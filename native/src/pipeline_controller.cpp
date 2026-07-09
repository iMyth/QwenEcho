/**
 * @file pipeline_controller.cpp
 * @brief Pipeline Controller — orchestrates the full audio processing pipeline.
 *
 * Creates, manages, and gracefully shuts down all pipeline components:
 *   AudioRingBuffer, BoundedSPSCQueues, AudioCollector, SentenceSegmenter,
 *   AsrStage, LlmStage, TtsStage, ThermalMonitor, MemoryMonitor,
 *   LatencyTracker.
 *
 * Pipeline wiring (cascade truncation):
 *   Audio Collector → Ring Buffer → VAD/Segmenter → ASR → LLM → TTS → Audio Output
 *
 *   Cascade truncation ensures downstream stages begin before upstream completes:
 *   - LLM begins translation as soon as ASR confirms text. The ASR→LLM
 *     BoundedSPSCQueue delivers confirmed text immediately; LLM does not
 *     wait for the audio segment to finish capturing or for the next segment.
 *   - TTS begins synthesis at punctuation boundaries before the full
 *     translation completes. The LLM emits partial results at punctuation
 *     into the LLM→TTS queue, and TTS dequeues and starts audio generation
 *     while LLM continues producing the remainder of the translation.
 *   - Each stage runs on its own worker thread, consuming from its input
 *     queue without blocking upstream, achieving overlapped execution.
 *
 * E2E latency budget:
 *   - Normal mode:   ASR ≤200ms + LLM ≤450ms + TTS ≤100ms = ≤800ms total
 *   - Throttle mode: Total E2E ≤1200ms
 *   - LatencyTracker monitors per-segment timestamps and reports
 *     MSG_LATENCY_WARNING when any stage or E2E exceeds budget.
 *
 * Graceful stop:
 *   1. Signal AudioCollector to stop (cease audio capture)
 *   2. Wait for SentenceSegmenter to flush locked segments through ASR→LLM→TTS
 *   3. Destroy all pipeline stages and threads
 *   4. Clear/discard unlocked audio remaining in ring buffer
 *   5. Complete all cleanup within 2 seconds
 *
 * Validates: Requirements 2.1, 2.2, 2.5, 8.1, 8.2, 8.3, 8.4, 8.5
 */

#include "pipeline_controller.h"
#include "audio_ring_buffer.h"
#include "bounded_spsc_queue.h"
#include "audio_collector.h"
#include "sentence_segmenter.h"
#include "asr_stage.h"
#include "llm_stage.h"
#include "tts_stage.h"
#include "thermal_monitor.h"
#include "memory_monitor.h"
#include "latency_tracker.h"
#include "hal_accelerator.h"
#include "gguf_inference.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#ifdef __APPLE__
#include <os/log.h>
#define ECHO_LOG(fmt, ...) do { os_log(OS_LOG_DEFAULT, fmt, ##__VA_ARGS__); fprintf(stderr, fmt "\n", ##__VA_ARGS__); } while(0)
#else
#define ECHO_LOG(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#endif

/* ─── Supported Languages ────────────────────────────────────────────────── */

/**
 * FunASR's 52-language support — common ISO 639-1 codes.
 * This list covers the primary languages plus extended support.
 */
static const char* const kSupportedLanguages[] = {
    "zh", "en", "ja", "ko", "fr", "de", "es", "it", "pt", "ru",
    "ar", "hi", "vi", "th", "id", "ms", "tl", "my", "km", "lo",
    "bn", "ta", "te", "mr", "gu", "kn", "ml", "pa", "ur", "ne",
    "si", "tr", "pl", "uk", "cs", "sk", "hu", "ro", "bg", "hr",
    "sr", "sl", "el", "nl", "sv", "da", "no", "fi", "et", "lv",
    "lt", "he"
};

static const size_t kSupportedLanguageCount =
    sizeof(kSupportedLanguages) / sizeof(kSupportedLanguages[0]);

/**
 * Check if a language code is in the supported list.
 */
static bool is_language_supported(const char* lang) {
    if (!lang || lang[0] == '\0') return false;
    for (size_t i = 0; i < kSupportedLanguageCount; ++i) {
        if (strcmp(lang, kSupportedLanguages[i]) == 0) {
            return true;
        }
    }
    return false;
}

/* ─── Ring Buffer Capacity ───────────────────────────────────────────────── */

/** 2^20 = 1,048,576 samples ≈ 65.5 seconds at 16kHz */
static constexpr uint32_t kRingBufferCapacity = 1048576;

/** Default sample rate for audio capture */
static constexpr uint32_t kAudioSampleRate = 16000;

/** Maximum time allowed for graceful stop (milliseconds) */
static constexpr int kStopDeadlineMs = 2000;

/** Polling interval during graceful flush (milliseconds) */
static constexpr int kFlushPollIntervalMs = 50;

/* ─── PipelineController struct definition ────────────────────────────────── */

struct PipelineController {
    std::mutex           mutex;
    std::atomic<bool>    running;

    /* Pipeline components (owned) */
    AudioRingBuffer*     ring_buffer;
    BoundedSPSCQueue<AsrToLlmElement>*  asr_to_llm_queue;
    BoundedSPSCQueue<LlmToTtsElement>*  llm_to_tts_queue;
    AudioCollector*      audio_collector;
    SentenceSegmenter*   sentence_segmenter;
    AsrStage*            asr_stage;
    LlmStage*            llm_stage;
    TtsStage*            tts_stage;
    ThermalMonitor*      thermal_monitor;
    MemoryMonitor*       memory_monitor;
    LatencyTracker*      latency_tracker;

    /* HAL accelerator (shared across stages) */
    AcceleratorContext*  accelerator;

    /* Feeder thread: reads audio from ring buffer → feeds sentence segmenter */
    std::thread           feeder_thread;
    std::atomic<bool>     feeder_running;
};

/* ─── Internal Helpers ───────────────────────────────────────────────────── */

/**
 * Callback from SentenceSegmenter when a segment is locked.
 * Routes the locked segment to the ASR stage for processing.
 */
static void on_segment_locked(const LockedSegment* segment, void* user) {
    auto* pc = static_cast<PipelineController*>(user);
    ECHO_LOG("[Pipeline] Segment locked: id=%u, samples=%u, speaker=%u",
             segment ? segment->segment_id : 0,
             segment ? segment->sample_count : 0,
             segment ? segment->speaker_id : 0);
    if (pc && pc->asr_stage && segment) {
        asr_stage_process_segment(pc->asr_stage, segment);
    }
}

/**
 * Callback for thermal mode changes.
 * Propagates the mode to ASR, LLM stages, and Latency Tracker.
 */
static void on_thermal_mode_change(ThermalMode mode, void* user) {
    auto* pc = static_cast<PipelineController*>(user);
    if (!pc) return;

    int throttle = (mode == THERMAL_NORMAL) ? 0 : 1;

    if (pc->asr_stage) {
        asr_stage_set_thermal_mode(pc->asr_stage, throttle);
    }
    if (pc->llm_stage) {
        llm_stage_set_thermal_mode(pc->llm_stage, throttle);
    }
    if (pc->latency_tracker) {
        latency_tracker_set_thermal_mode(pc->latency_tracker, throttle);
    }
}

/**
 * Callback for memory pressure events.
 * Level 2 triggers graceful pipeline stop.
 */
static void on_memory_pressure(MemoryLevel level, size_t current_bytes,
                               size_t limit_bytes, void* user) {
    auto* pc = static_cast<PipelineController*>(user);
    if (!pc) return;

    if (level == MEM_LEVEL_CRITICAL && pc->running.load(std::memory_order_acquire)) {
        /* Level 2 (95%): trigger graceful pipeline stop.
         * Note: In a real system this would signal the EngineManager.
         * Here we stop directly, but avoid recursion via the running flag. */
        pipeline_controller_stop(pc);
    }
}

/**
 * Feeder thread: continuously reads audio from the ring buffer and feeds it
 * to the sentence segmenter for VAD processing and sentence boundary detection.
 *
 * This thread bridges the gap between the audio collector (producer → ring buffer)
 * and the sentence segmenter (consumer of audio frames). Without this thread,
 * audio is captured into the ring buffer but never processed.
 *
 * Reads in chunks of 1600 samples (100ms at 16kHz) for low latency.
 */
static void feeder_loop(PipelineController* pc) {
    constexpr uint32_t READ_CHUNK_SIZE = 1600; /* 100ms at 16kHz */
    int16_t read_buffer[READ_CHUNK_SIZE];
    uint32_t read_counter = 0;
    uint32_t total_samples = 0;

    while (pc->feeder_running.load(std::memory_order_acquire)) {
        uint32_t avail = pc->ring_buffer->available();
        if (avail == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        uint32_t to_read = (avail < READ_CHUNK_SIZE) ? avail : READ_CHUNK_SIZE;
        uint32_t read_count = pc->ring_buffer->read(read_buffer, to_read);

        if (read_count > 0 && pc->sentence_segmenter) {
            sentence_segmenter_feed_audio(pc->sentence_segmenter,
                                           read_buffer, read_count, 0);
            total_samples += read_count;
            read_counter++;
            /* Log every 50 reads (~5 seconds of audio) */
            if (read_counter % 50 == 1) {
                ECHO_LOG("[Pipeline] Feeder: %u reads, %u samples, segmenter state=%d",
                         read_counter, total_samples,
                         sentence_segmenter_get_state(pc->sentence_segmenter));
            }
        }
    }
    ECHO_LOG("[Pipeline] Feeder thread stopped (%u total reads, %u samples)",
             read_counter, total_samples);
}

/**
 * Destroy all pipeline resources. Must be called with mutex held.
 */
static void destroy_pipeline_resources(PipelineController* pc) {
    /* Stop feeder thread first (it reads from ring buffer and feeds segmenter) */
    pc->feeder_running.store(false, std::memory_order_release);
    if (pc->feeder_thread.joinable()) {
        pc->feeder_thread.join();
    }

    /* Destroy stages in reverse-pipeline order (TTS → LLM → ASR) */
    if (pc->tts_stage) {
        tts_stage_destroy(pc->tts_stage);
        pc->tts_stage = nullptr;
    }
    if (pc->llm_stage) {
        llm_stage_destroy(pc->llm_stage);
        pc->llm_stage = nullptr;
    }
    if (pc->asr_stage) {
        asr_stage_destroy(pc->asr_stage);
        pc->asr_stage = nullptr;
    }

    /* Destroy sentence segmenter */
    if (pc->sentence_segmenter) {
        sentence_segmenter_destroy(pc->sentence_segmenter);
        pc->sentence_segmenter = nullptr;
    }

    /* Destroy audio collector */
    if (pc->audio_collector) {
        audio_collector_destroy(pc->audio_collector);
        pc->audio_collector = nullptr;
    }

    /* Destroy monitors */
    if (pc->thermal_monitor) {
        thermal_monitor_destroy(pc->thermal_monitor);
        pc->thermal_monitor = nullptr;
    }
    if (pc->memory_monitor) {
        memory_monitor_destroy(pc->memory_monitor);
        pc->memory_monitor = nullptr;
    }
    if (pc->latency_tracker) {
        latency_tracker_destroy(pc->latency_tracker);
        pc->latency_tracker = nullptr;
    }

    /* Destroy queues */
    if (pc->asr_to_llm_queue) {
        delete pc->asr_to_llm_queue;
        pc->asr_to_llm_queue = nullptr;
    }
    if (pc->llm_to_tts_queue) {
        delete pc->llm_to_tts_queue;
        pc->llm_to_tts_queue = nullptr;
    }

    /* Destroy ring buffer */
    if (pc->ring_buffer) {
        delete pc->ring_buffer;
        pc->ring_buffer = nullptr;
    }

    /* Destroy accelerator */
    if (pc->accelerator) {
        hal_accelerator_destroy(pc->accelerator);
        pc->accelerator = nullptr;
    }
}

/* ─── Public API ─────────────────────────────────────────────────────────── */

PipelineController* pipeline_controller_create(void) {
    auto* pc = static_cast<PipelineController*>(calloc(1, sizeof(PipelineController)));
    if (!pc) return nullptr;

    /* Placement-new for C++ members (calloc gives raw memory) */
    new (&pc->mutex) std::mutex();
    new (&pc->running) std::atomic<bool>(false);
    new (&pc->feeder_thread) std::thread();
    new (&pc->feeder_running) std::atomic<bool>(false);

    pc->ring_buffer = nullptr;
    pc->asr_to_llm_queue = nullptr;
    pc->llm_to_tts_queue = nullptr;
    pc->audio_collector = nullptr;
    pc->sentence_segmenter = nullptr;
    pc->asr_stage = nullptr;
    pc->llm_stage = nullptr;
    pc->tts_stage = nullptr;
    pc->thermal_monitor = nullptr;
    pc->memory_monitor = nullptr;
    pc->latency_tracker = nullptr;
    pc->accelerator = nullptr;

    return pc;
}

int pipeline_controller_start(PipelineController* pc, const char* src_lang,
                              const char* tgt_lang,
                              const char* asr_path,
                              const char* llm_path,
                              const char* tts_path) {
    if (!pc) return ECHO_ERR_NOT_INITIALIZED;

    std::lock_guard<std::mutex> lock(pc->mutex);

    /* Guard: no duplicate session */
    if (pc->running.load(std::memory_order_relaxed)) {
        return ECHO_ERR_SESSION_ACTIVE;
    }

    /* Validate language pair */
    if (!is_language_supported(src_lang)) {
        return ECHO_ERR_UNSUPPORTED_LANG;
    }
    if (!is_language_supported(tgt_lang)) {
        return ECHO_ERR_UNSUPPORTED_LANG;
    }

    /* ─── Create pipeline resources ──────────────────────────────────── */

    /* 1. Create HAL accelerator */
    pc->accelerator = hal_accelerator_create();
    /* accelerator may be NULL in stub mode — stages handle that gracefully */

    /* 2. Create ring buffer (2^20 samples ≈ 65.5s at 16kHz) */
    pc->ring_buffer = new(std::nothrow) AudioRingBuffer(kRingBufferCapacity);
    if (!pc->ring_buffer) {
        destroy_pipeline_resources(pc);
        return ECHO_ERR_MEMORY;
    }

    /* 3. Create inter-stage bounded SPSC queues (capacity=64) */
    pc->asr_to_llm_queue = new(std::nothrow) BoundedSPSCQueue<AsrToLlmElement>();
    if (!pc->asr_to_llm_queue) {
        destroy_pipeline_resources(pc);
        return ECHO_ERR_MEMORY;
    }

    pc->llm_to_tts_queue = new(std::nothrow) BoundedSPSCQueue<LlmToTtsElement>();
    if (!pc->llm_to_tts_queue) {
        destroy_pipeline_resources(pc);
        return ECHO_ERR_MEMORY;
    }

    /* 4. Create Audio Collector (writes to ring buffer) */
    pc->audio_collector = audio_collector_create(pc->ring_buffer);
    if (!pc->audio_collector) {
        destroy_pipeline_resources(pc);
        return ECHO_ERR_MEMORY;
    }

    /* 5. Create ASR stage (reads from segmenter, writes to asr→llm queue) */
    pc->asr_stage = asr_stage_create(pc->accelerator, pc->asr_to_llm_queue, asr_path);
    if (!pc->asr_stage) {
        destroy_pipeline_resources(pc);
        return ECHO_ERR_MEMORY;
    }

    /* 6. Create Sentence Segmenter (reads from ring buffer, dispatches to ASR) */
    pc->sentence_segmenter = sentence_segmenter_create(
        kAudioSampleRate, on_segment_locked, pc);
    if (!pc->sentence_segmenter) {
        destroy_pipeline_resources(pc);
        return ECHO_ERR_MEMORY;
    }

    /* 7. Create LLM stage (reads from asr→llm queue, writes to llm→tts queue) */
    pc->llm_stage = llm_stage_create(pc->accelerator,
                                     pc->asr_to_llm_queue,
                                     pc->llm_to_tts_queue,
                                     llm_path);
    if (!pc->llm_stage) {
        destroy_pipeline_resources(pc);
        return ECHO_ERR_MEMORY;
    }

    /* 8. Create TTS stage (reads from llm→tts queue, outputs audio) */
    pc->tts_stage = tts_stage_create(pc->accelerator, pc->llm_to_tts_queue, tts_path);
    if (!pc->tts_stage) {
        destroy_pipeline_resources(pc);
        return ECHO_ERR_MEMORY;
    }

    /* 9. Create Thermal Monitor */
    pc->thermal_monitor = thermal_monitor_create(on_thermal_mode_change, pc);
    if (!pc->thermal_monitor) {
        destroy_pipeline_resources(pc);
        return ECHO_ERR_MEMORY;
    }

    /* 10. Create Memory Monitor */
    pc->memory_monitor = memory_monitor_create(on_memory_pressure, pc);
    if (!pc->memory_monitor) {
        destroy_pipeline_resources(pc);
        return ECHO_ERR_MEMORY;
    }

    /* 11. Create Latency Tracker for E2E SLA monitoring */
    pc->latency_tracker = latency_tracker_create();
    if (!pc->latency_tracker) {
        destroy_pipeline_resources(pc);
        return ECHO_ERR_MEMORY;
    }

    /* ─── Start all components ───────────────────────────────────────── */

    /* Start monitors first (they observe, don't produce data) */
    thermal_monitor_start(pc->thermal_monitor);
    memory_monitor_start(pc->memory_monitor);

    /* Start audio collector (begins filling ring buffer) */
    int rc = audio_collector_start(pc->audio_collector);
    if (rc != 0) {
        destroy_pipeline_resources(pc);
        return ECHO_ERR_MEMORY;
    }

    /* Start feeder thread: reads ring buffer → feeds sentence segmenter */
    pc->feeder_running.store(true, std::memory_order_relaxed);
    pc->feeder_thread = std::thread(feeder_loop, pc);
    ECHO_LOG("[Pipeline] Feeder thread started");

    /* Mark pipeline as running */
    pc->running.store(true, std::memory_order_release);

    return ECHO_OK;
}

int pipeline_controller_stop(PipelineController* pc) {
    if (!pc) return ECHO_ERR_NOT_INITIALIZED;

    std::lock_guard<std::mutex> lock(pc->mutex);

    /* No-op if not running */
    if (!pc->running.load(std::memory_order_relaxed)) {
        return ECHO_OK;
    }

    /* ─── Graceful stop sequence ─────────────────────────────────────── */

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(kStopDeadlineMs);

    /*
     * Step 1: Stop AudioCollector (no new audio enters the ring buffer).
     * This prevents new data from arriving while we flush.
     */
    if (pc->audio_collector) {
        audio_collector_stop(pc->audio_collector);
    }

    /*
     * Step 2: Wait for Sentence Segmenter to flush any locked segments.
     * Locked segments (in Accumulating/Locking state) are already being
     * processed through ASR→LLM→TTS. We give the pipeline time to drain
     * in-flight work, bounded by the 2-second deadline.
     *
     * We poll the inter-stage queues: when both are empty and the segmenter
     * is idle, all locked segments have been processed.
     */
    while (std::chrono::steady_clock::now() < deadline) {
        bool segmenter_idle = true;
        if (pc->sentence_segmenter) {
            segmenter_idle =
                (sentence_segmenter_get_state(pc->sentence_segmenter) == SEG_STATE_IDLE);
        }

        bool queues_drained = true;
        if (pc->asr_to_llm_queue && pc->asr_to_llm_queue->size() > 0) {
            queues_drained = false;
        }
        if (pc->llm_to_tts_queue && pc->llm_to_tts_queue->size() > 0) {
            queues_drained = false;
        }

        if (segmenter_idle && queues_drained) {
            break;  /* All locked segments have been flushed */
        }

        /* Brief sleep to avoid busy-waiting, then re-check */
        std::this_thread::sleep_for(std::chrono::milliseconds(kFlushPollIntervalMs));
    }

    /*
     * Step 3: Destroy all pipeline stages and threads.
     * Each stage's destroy function joins its worker thread.
     */

    /*
     * Step 4: Discard unlocked audio remaining in the ring buffer.
     * This is handled implicitly by destroying the ring buffer.
     */

    /*
     * Step 5: Release all pipeline resources.
     */
    destroy_pipeline_resources(pc);

    /* Mark pipeline as stopped */
    pc->running.store(false, std::memory_order_release);

    return ECHO_OK;
}

bool pipeline_controller_is_running(const PipelineController* pc) {
    if (!pc) return false;
    return pc->running.load(std::memory_order_acquire);
}

void pipeline_controller_destroy(PipelineController* pc) {
    if (!pc) return;

    /* Stop pipeline if running */
    pipeline_controller_stop(pc);

    /* Explicitly destroy C++ members before freeing raw memory */
    pc->mutex.~mutex();
    pc->running.~atomic<bool>();
    pc->feeder_thread.~thread();
    pc->feeder_running.~atomic<bool>();

    free(pc);
}
