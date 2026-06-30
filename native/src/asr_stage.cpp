/**
 * ASR Processing Stage — Implementation.
 *
 * Architecture:
 * - A worker thread pulls LockedSegments from an internal queue and processes
 *   them sequentially.
 * - Inference is performed via the HAL accelerator (stub: energy-based text
 *   generation since the real FunASR-Nano model is not available at build time).
 * - Partial tokens are streamed to the UI via Native Port (MSG_ASR_PARTIAL).
 * - Confirmed text is sent via MSG_ASR_CONFIRMED and enqueued into the
 *   ASR→LLM bounded queue.
 * - SLA: ≤200ms from segment receipt to first partial character.
 * - Throttle mode: resample 16kHz→8kHz before inference.
 *
 * Validates: Requirements 5.1, 5.2, 5.3, 5.4, 5.5, 5.6, 10.5
 */

#include "asr_stage.h"
#include "bounded_spsc_queue.h"
#include "native_port.h"
#include "hal_accelerator.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <cstdint>
#include <cstdio>

/* --------------------------------------------------------------------------
 * Constants
 * -------------------------------------------------------------------------- */

/** SLA budget: first partial character must arrive within 200ms. */
static constexpr int32_t ASR_LATENCY_BUDGET_MS = 200;

/** Energy threshold below which audio is classified as noise/unintelligible. */
static constexpr int32_t NOISE_ENERGY_THRESHOLD = 150;

/** Source sample rate (from microphone/ring buffer). */
static constexpr uint32_t SOURCE_SAMPLE_RATE = 16000;

/* --------------------------------------------------------------------------
 * Internal segment copy (owns audio data)
 * -------------------------------------------------------------------------- */

struct SegmentCopy {
    std::vector<int16_t> audio_data;
    uint32_t segment_id;
    uint8_t speaker_id;
    uint64_t timestamp_ms;
    std::chrono::steady_clock::time_point enqueue_time;
};

/* --------------------------------------------------------------------------
 * AsrStage internal structure
 * -------------------------------------------------------------------------- */

struct AsrStage {
    /* HAL accelerator for inference (may be nullptr in stub mode) */
    AcceleratorContext* accelerator;

    /* Output queue: confirmed text → LLM stage */
    BoundedSPSCQueue<AsrToLlmElement>* output_queue;

    /* Thermal mode: 0 = Normal, 1 = Throttle */
    std::atomic<int> throttle_mode;

    /* Worker thread */
    std::thread worker;
    std::atomic<bool> running;

    /* Internal segment queue (protected by mutex) */
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::queue<SegmentCopy> pending_segments;
};

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/**
 * Compute mean absolute energy of audio samples.
 * Returns the mean absolute amplitude value.
 */
static int32_t compute_audio_energy(const int16_t* samples, uint32_t count) {
    if (count == 0) return 0;

    int64_t sum = 0;
    for (uint32_t i = 0; i < count; ++i) {
        int32_t val = samples[i];
        sum += (val < 0) ? -val : val;
    }

    return static_cast<int32_t>(sum / count);
}

/**
 * Simple downsampling from 16kHz to 8kHz.
 * Uses a simple averaging of adjacent sample pairs (low-pass + decimation).
 */
static std::vector<int16_t> downsample_16k_to_8k(const int16_t* samples, uint32_t count) {
    uint32_t output_count = count / 2;
    std::vector<int16_t> output(output_count);

    for (uint32_t i = 0; i < output_count; ++i) {
        /* Average adjacent samples for basic anti-aliasing */
        int32_t sum = static_cast<int32_t>(samples[i * 2]) +
                      static_cast<int32_t>(samples[i * 2 + 1]);
        output[i] = static_cast<int16_t>(sum / 2);
    }

    return output;
}

/**
 * Stub ASR inference: generates pseudo-text based on audio characteristics.
 *
 * In production, this would call hal_accelerator_infer() with the FunASR-Nano
 * model. For now, we simulate by analyzing audio energy patterns to produce
 * plausible partial and confirmed text tokens.
 *
 * Returns a vector of "tokens" representing the transcription result.
 * Empty vector means the segment is noise/unintelligible.
 */
static std::vector<std::string> stub_infer_tokens(AcceleratorContext* /*accelerator*/,
                                                   const int16_t* samples,
                                                   uint32_t sample_count) {
    std::vector<std::string> tokens;

    int32_t energy = compute_audio_energy(samples, sample_count);

    /* Below noise threshold → unintelligible, discard */
    if (energy <= NOISE_ENERGY_THRESHOLD) {
        return tokens; /* empty = noise */
    }

    /* Simulate token generation based on audio length and energy.
     * In production: FunASR-Nano streaming decoder produces actual tokens. */

    /* Estimate "word count" from segment duration (rough heuristic) */
    float duration_sec = static_cast<float>(sample_count) / SOURCE_SAMPLE_RATE;
    int word_count = static_cast<int>(duration_sec * 3.0f); /* ~3 words/second */
    if (word_count < 1) word_count = 1;
    if (word_count > 20) word_count = 20;

    /* Generate placeholder tokens */
    for (int i = 0; i < word_count; ++i) {
        char token_buf[32];
        std::snprintf(token_buf, sizeof(token_buf), "word%d", i + 1);
        tokens.emplace_back(token_buf);
    }

    return tokens;
}

/**
 * ASR worker thread main loop.
 * Pulls segments from the internal queue and processes them.
 */
static void asr_worker_loop(AsrStage* stage) {
    while (stage->running.load(std::memory_order_acquire)) {
        SegmentCopy segment;
        bool have_segment = false;

        /* Wait for a segment or shutdown signal */
        {
            std::unique_lock<std::mutex> lock(stage->queue_mutex);
            stage->queue_cv.wait(lock, [&] {
                return !stage->pending_segments.empty() ||
                       !stage->running.load(std::memory_order_acquire);
            });

            if (!stage->pending_segments.empty()) {
                segment = std::move(stage->pending_segments.front());
                stage->pending_segments.pop();
                have_segment = true;
            }
        }

        if (!have_segment) {
            continue;
        }

        /* --- Process the segment --- */

        /* Step 1: Resample if in Throttle mode */
        const int16_t* infer_samples = segment.audio_data.data();
        uint32_t infer_count = static_cast<uint32_t>(segment.audio_data.size());
        std::vector<int16_t> resampled;

        if (stage->throttle_mode.load(std::memory_order_acquire) != 0) {
            resampled = downsample_16k_to_8k(infer_samples, infer_count);
            infer_samples = resampled.data();
            infer_count = static_cast<uint32_t>(resampled.size());
        }

        /* Step 2: Run inference (stub) */
        std::vector<std::string> tokens = stub_infer_tokens(
            stage->accelerator, infer_samples, infer_count);

        /* Step 3: Check if noise/unintelligible → silent discard */
        if (tokens.empty()) {
            /* Silently discard: no MSG_ASR_CONFIRMED, no queue push */
            continue;
        }

        /* Step 4: Stream partial tokens via MSG_ASR_PARTIAL */
        bool first_partial_sent = false;
        std::string accumulated_text;

        for (size_t i = 0; i < tokens.size(); ++i) {
            if (i > 0) accumulated_text += " ";
            accumulated_text += tokens[i];

            /* Send partial result (all tokens accumulated so far) */
            native_port_post_asr_partial(
                segment.speaker_id,
                accumulated_text.c_str(),
                segment.timestamp_ms);

            /* Track first partial for SLA measurement */
            if (!first_partial_sent) {
                first_partial_sent = true;

                /* Measure latency from segment enqueue to first partial */
                auto first_char_time = std::chrono::steady_clock::now();
                auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                    first_char_time - segment.enqueue_time);
                int32_t latency_ms = static_cast<int32_t>(latency.count());

                /* Step 5: Report SLA violation if > 200ms */
                if (latency_ms > ASR_LATENCY_BUDGET_MS) {
                    native_port_post_latency_warning("ASR", latency_ms, ASR_LATENCY_BUDGET_MS);
                }
            }
        }

        /* Step 6: Finalize confirmed text (the full accumulated text) */
        const std::string& confirmed_text = accumulated_text;

        native_port_post_asr_confirmed(
            segment.speaker_id,
            confirmed_text.c_str(),
            segment.timestamp_ms,
            segment.segment_id);

        /* Step 7: Enqueue into ASR→LLM bounded queue */
        AsrToLlmElement element;
        element.segment_id = segment.segment_id;
        element.speaker_id = segment.speaker_id;
        element.timestamp_ms = segment.timestamp_ms;

        /* Copy text into fixed-size buffer (truncate if too long) */
        size_t copy_len = confirmed_text.size();
        if (copy_len >= sizeof(element.text)) {
            copy_len = sizeof(element.text) - 1;
        }
        std::memcpy(element.text, confirmed_text.c_str(), copy_len);
        element.text[copy_len] = '\0';
        element.text_len = static_cast<uint16_t>(copy_len);

        stage->output_queue->try_push(element);
    }
}

/* --------------------------------------------------------------------------
 * Public API implementation
 * -------------------------------------------------------------------------- */

AsrStage* asr_stage_create(AcceleratorContext* accelerator,
                           BoundedSPSCQueue<AsrToLlmElement>* output_queue) {
    if (!output_queue) return nullptr;

    AsrStage* stage = new (std::nothrow) AsrStage();
    if (!stage) return nullptr;

    stage->accelerator = accelerator;
    stage->output_queue = output_queue;
    stage->throttle_mode.store(0, std::memory_order_relaxed);
    stage->running.store(true, std::memory_order_relaxed);

    /* Launch worker thread */
    stage->worker = std::thread(asr_worker_loop, stage);

    return stage;
}

extern "C" {

int asr_stage_process_segment(AsrStage* stage, const LockedSegment* segment) {
    if (!stage || !segment) return -1;
    if (!segment->audio_data || segment->sample_count == 0) return -1;

    /* Create an owned copy of the segment data */
    SegmentCopy copy;
    copy.audio_data.assign(segment->audio_data,
                           segment->audio_data + segment->sample_count);
    copy.segment_id = segment->segment_id;
    copy.speaker_id = segment->speaker_id;
    copy.timestamp_ms = segment->timestamp_ms;
    copy.enqueue_time = std::chrono::steady_clock::now();

    /* Enqueue for async processing */
    {
        std::lock_guard<std::mutex> lock(stage->queue_mutex);
        stage->pending_segments.push(std::move(copy));
    }
    stage->queue_cv.notify_one();

    return 0;
}

void asr_stage_set_thermal_mode(AsrStage* stage, int throttle_mode) {
    if (!stage) return;
    stage->throttle_mode.store(throttle_mode, std::memory_order_release);
}

void asr_stage_destroy(AsrStage* stage) {
    if (!stage) return;

    /* Signal worker to stop */
    stage->running.store(false, std::memory_order_release);
    stage->queue_cv.notify_all();

    /* Wait for worker to finish */
    if (stage->worker.joinable()) {
        stage->worker.join();
    }

    delete stage;
}

} /* extern "C" */
