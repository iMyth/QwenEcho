/**
 * TTS Synthesis Stage — Implementation.
 *
 * Architecture:
 * - A worker thread polls the LLM→TTS input queue for translated text.
 * - For each dequeued element:
 *   1. Checks if text is whitespace-only or punctuation-only → discards
 *      without sending TTS_STARTED (Requirement 7.6).
 *   2. Sends MSG_TTS_STARTED via Native Port.
 *   3. Runs TTS inference (stub: generates simulated PCM audio).
 *   4. Tracks TTFA (Time To First Audio): if >100ms, reports
 *      MSG_LATENCY_WARNING("TTS", actual_ms, 100) (Requirement 8.4).
 *   5. Outputs PCM audio at 24kHz, 16-bit, mono to an internal buffer
 *      (in production, feeds to platform speaker via HAL audio output).
 *   6. Sends MSG_TTS_COMPLETE via Native Port.
 * - On failure: logs error, skips segment, continues processing next
 *   (no pipeline halt/restart) (Requirement 7.5).
 * - Operates on its own thread (concurrent with ASR/LLM) (Requirement 7.3).
 *
 * Validates: Requirements 7.1, 7.2, 7.3, 7.4, 7.5, 7.6, 8.4
 */

#include "tts_stage.h"
#include "bounded_spsc_queue.h"
#include "native_port.h"
#include "hal_accelerator.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cctype>

/* --------------------------------------------------------------------------
 * Constants
 * -------------------------------------------------------------------------- */

/** SLA budget: TTFA must be ≤100ms. */
static constexpr int32_t TTS_LATENCY_BUDGET_MS = 100;

/** Output audio format: 24kHz sample rate. */
static constexpr uint32_t TTS_SAMPLE_RATE = 24000;

/** Audio output buffer size: ~500ms of audio for jitter absorption.
 *  At 24kHz mono, 500ms = 12000 samples. */
static constexpr uint32_t TTS_OUTPUT_BUFFER_SAMPLES = 12000;

/** Polling interval when input queue is empty (ms). */
static constexpr int POLL_INTERVAL_MS = 5;

/** Approximate speaking rate: samples per character of input text.
 *  Used for stub synthesis to generate realistic-length audio.
 *  At ~150 words/minute and average 5 chars/word:
 *  24000 samples/second / (150*5/60 chars/second) ≈ 1920 samples/char.
 *  Simplified to 1920 for the stub. */
static constexpr uint32_t SAMPLES_PER_CHAR = 1920;

/* --------------------------------------------------------------------------
 * TtsStage internal structure
 * -------------------------------------------------------------------------- */

struct TtsStage {
    /* HAL accelerator for inference (may be nullptr in stub mode) */
    AcceleratorContext* accelerator;

    /* Input queue: translated text from LLM (producer: LLM stage) */
    BoundedSPSCQueue<LlmToTtsElement>* input_queue;

    /* Worker thread */
    std::thread worker;
    std::atomic<bool> running;

    /* Audio output buffer (simulates platform speaker feed).
     * In production, this would be replaced by a ring buffer feeding
     * the HAL audio output. */
    std::vector<int16_t> output_buffer;
};

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/**
 * Check if a character is considered punctuation for the discard check.
 * Includes common sentence-ending and mid-sentence punctuation.
 */
static bool is_discard_punctuation(char c) {
    return c == '.' || c == ',' || c == ';' || c == ':' ||
           c == '!' || c == '?' || c == '-' || c == '\'' ||
           c == '"' || c == '(' || c == ')' || c == '[' ||
           c == ']' || c == '{' || c == '}' || c == '/' ||
           c == '\\' || c == '|' || c == '@' || c == '#' ||
           c == '$' || c == '%' || c == '^' || c == '&' ||
           c == '*' || c == '_' || c == '+' || c == '=' ||
           c == '~' || c == '`' || c == '<' || c == '>';
}

/**
 * Check if a text segment should be discarded.
 *
 * A segment is discarded if it consists entirely of whitespace characters
 * and/or punctuation characters with no translatable content.
 *
 * @param text     Pointer to UTF-8 text.
 * @param text_len Length of text in bytes.
 * @return true if the segment should be discarded (no TTS output).
 */
static bool should_discard_segment(const char* text, uint16_t text_len) {
    if (text_len == 0) return true;

    for (uint16_t i = 0; i < text_len; ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        /* Skip whitespace */
        if (std::isspace(c)) continue;
        /* Skip punctuation */
        if (is_discard_punctuation(static_cast<char>(c))) continue;
        /* Found a non-whitespace, non-punctuation character — keep segment */
        return false;
    }

    /* All characters were whitespace or punctuation */
    return true;
}

/**
 * Stub TTS inference: generates simulated PCM audio based on text length.
 *
 * In production, this would call hal_accelerator_infer() with the
 * Qwen3-TTS-Streaming model. For now, generates a simple low-amplitude
 * tone to simulate audio output.
 *
 * @param accelerator  HAL accelerator context (unused in stub).
 * @param text         Input text to synthesize.
 * @param text_len     Length of input text.
 * @param output       Output vector to receive PCM samples (24kHz, 16-bit, mono).
 * @return 0 on success, negative on failure.
 */
static int stub_tts_synthesize(AcceleratorContext* /*accelerator*/,
                               const char* text,
                               uint16_t text_len,
                               std::vector<int16_t>& output) {
    /* Calculate output length based on text length.
     * Cap at a reasonable maximum to avoid excessive memory usage. */
    uint32_t num_samples = static_cast<uint32_t>(text_len) * SAMPLES_PER_CHAR;

    /* Cap at 5 seconds of audio (120000 samples at 24kHz) */
    static constexpr uint32_t MAX_SAMPLES = TTS_SAMPLE_RATE * 5;
    if (num_samples > MAX_SAMPLES) {
        num_samples = MAX_SAMPLES;
    }

    /* Minimum output: 100ms of audio (2400 samples) for any non-empty text */
    static constexpr uint32_t MIN_SAMPLES = TTS_SAMPLE_RATE / 10;
    if (num_samples < MIN_SAMPLES) {
        num_samples = MIN_SAMPLES;
    }

    output.resize(num_samples);

    /* Generate a simple sawtooth wave at low amplitude as placeholder audio.
     * In production, this buffer would be filled by the TTS model inference.
     * Period in samples = sample_rate / frequency (440Hz tone). */
    static constexpr int16_t AMPLITUDE = 1000; /* Low amplitude to avoid loudness */
    static constexpr uint32_t PERIOD_SAMPLES = TTS_SAMPLE_RATE / 440;

    for (uint32_t i = 0; i < num_samples; ++i) {
        /* Sawtooth: linearly ramp from -AMPLITUDE to +AMPLITUDE over one period */
        uint32_t pos_in_period = i % PERIOD_SAMPLES;
        int32_t sample_val = static_cast<int32_t>(
            AMPLITUDE * (2.0 * static_cast<double>(pos_in_period) /
                         static_cast<double>(PERIOD_SAMPLES) - 1.0));
        /* Clamp to int16 range */
        if (sample_val > 32767) sample_val = 32767;
        if (sample_val < -32768) sample_val = -32768;
        output[i] = static_cast<int16_t>(sample_val);
    }

    (void)text; /* Suppress unused parameter warning */

    return 0;
}

/**
 * TTS worker thread main loop.
 * Polls the input queue for translated text and synthesizes speech.
 */
static void tts_worker_loop(TtsStage* stage) {
    while (stage->running.load(std::memory_order_acquire)) {
        LlmToTtsElement input;

        /* Poll the input queue (lock-free) */
        if (!stage->input_queue->try_pop(input)) {
            /* Queue empty — sleep briefly and retry */
            std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
            continue;
        }

        /* -----------------------------------------------------------
         * Step 1: Check if text is whitespace-only or punctuation-only.
         *         If so, discard without sending TTS_STARTED.
         * ----------------------------------------------------------- */
        if (should_discard_segment(input.text, input.text_len)) {
            /* Discard: no audio output, no TTS_STARTED event */
            continue;
        }

        /* -----------------------------------------------------------
         * Step 2: Send MSG_TTS_STARTED via Native Port.
         * ----------------------------------------------------------- */
        native_port_post_tts_started(input.speaker_id, input.segment_id);

        /* Record time immediately after TTS_STARTED for TTFA measurement */
        auto synthesis_start = std::chrono::steady_clock::now();

        /* -----------------------------------------------------------
         * Step 3: Run TTS inference (stub: generate simulated PCM audio).
         * ----------------------------------------------------------- */
        std::vector<int16_t> pcm_output;
        int synth_result = stub_tts_synthesize(
            stage->accelerator, input.text, input.text_len, pcm_output);

        /* -----------------------------------------------------------
         * Step 4: Track TTFA and report SLA violations.
         * ----------------------------------------------------------- */
        auto first_audio_time = std::chrono::steady_clock::now();
        auto ttfa = std::chrono::duration_cast<std::chrono::milliseconds>(
            first_audio_time - synthesis_start);
        int32_t ttfa_ms = static_cast<int32_t>(ttfa.count());

        if (ttfa_ms > TTS_LATENCY_BUDGET_MS) {
            native_port_post_latency_warning("TTS", ttfa_ms, TTS_LATENCY_BUDGET_MS);
        }

        /* -----------------------------------------------------------
         * Step 5: Handle synthesis result.
         * ----------------------------------------------------------- */
        if (synth_result != 0) {
            /* Synthesis failed: log error, skip segment, continue.
             * No pipeline halt or restart (Requirement 7.5). */
            std::fprintf(stderr,
                         "[TTS] Synthesis failed for segment %u (speaker %u): error %d. "
                         "Skipping segment.\n",
                         input.segment_id, input.speaker_id, synth_result);

            /* Still send TTS_COMPLETE to maintain lifecycle consistency */
            native_port_post_tts_complete(input.speaker_id, input.segment_id);
            continue;
        }

        /* -----------------------------------------------------------
         * Step 6: Output PCM audio to platform speaker.
         *         In production: feed to HAL audio output ring buffer.
         *         Stub: write to internal output buffer.
         * ----------------------------------------------------------- */
        if (!pcm_output.empty()) {
            /* Append to output buffer (simulating speaker feed).
             * In production, this would write to a secondary ring buffer
             * connected to hal_audio_output. We keep the last buffer
             * worth of samples for potential playback verification. */
            stage->output_buffer = std::move(pcm_output);
        }

        /* -----------------------------------------------------------
         * Step 7: Send MSG_TTS_COMPLETE via Native Port.
         * ----------------------------------------------------------- */
        native_port_post_tts_complete(input.speaker_id, input.segment_id);
    }
}

/* --------------------------------------------------------------------------
 * Public API implementation
 * -------------------------------------------------------------------------- */

TtsStage* tts_stage_create(AcceleratorContext* accelerator,
                           BoundedSPSCQueue<LlmToTtsElement>* input_queue) {
    if (!input_queue) return nullptr;

    TtsStage* stage = new (std::nothrow) TtsStage();
    if (!stage) return nullptr;

    stage->accelerator = accelerator;
    stage->input_queue = input_queue;
    stage->running.store(true, std::memory_order_relaxed);

    /* Pre-allocate output buffer capacity (~500ms of audio) */
    stage->output_buffer.reserve(TTS_OUTPUT_BUFFER_SAMPLES);

    /* Launch worker thread */
    stage->worker = std::thread(tts_worker_loop, stage);

    return stage;
}

extern "C" {

void tts_stage_destroy(TtsStage* stage) {
    if (!stage) return;

    /* Signal worker to stop */
    stage->running.store(false, std::memory_order_release);

    /* Wait for worker to finish */
    if (stage->worker.joinable()) {
        stage->worker.join();
    }

    delete stage;
}

} /* extern "C" */
