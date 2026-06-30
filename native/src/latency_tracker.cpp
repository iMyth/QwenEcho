/**
 * @file latency_tracker.cpp
 * @brief E2E Latency Tracker — implementation.
 *
 * Maintains a circular buffer of LatencyRecords (one per in-flight segment)
 * and checks stage-level and E2E latency budgets. When a budget is violated,
 * posts MSG_LATENCY_WARNING via the Native Port with the offending stage name,
 * actual latency, and budget.
 *
 * Cascade truncation documentation:
 *   The pipeline achieves sub-800ms E2E latency through cascade processing:
 *   - LLM begins translation as soon as ASR confirms text (no waiting for
 *     the audio segment to fully finish capturing).
 *   - TTS begins synthesis at punctuation boundaries before the full
 *     translation completes (streaming token-by-token from LLM to TTS).
 *   - This cascade is implemented via the BoundedSPSCQueues between stages:
 *     ASR enqueues confirmed text → LLM dequeues immediately → LLM enqueues
 *     partial translation at punctuation → TTS dequeues and begins synthesis.
 *   - Each stage operates on its own thread, consuming from its input queue
 *     without waiting for upstream to complete its current segment.
 *
 * Validates: Requirements 8.1, 8.2, 8.3, 8.4, 8.5
 */

#include "latency_tracker.h"
#include "native_port.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>

/* ─── Constants ──────────────────────────────────────────────────────────── */

/** Maximum number of in-flight segments tracked simultaneously. */
static constexpr uint32_t kMaxTrackedSegments = 32;

/* ─── Stage name strings for MSG_LATENCY_WARNING ──────────────────────── */

static const char* const kStageASR  = "ASR";
static const char* const kStageLLM  = "LLM";
static const char* const kStageTTS  = "TTS";
static const char* const kStageE2E  = "E2E";

/* ─── LatencyTracker struct ──────────────────────────────────────────────── */

struct LatencyTracker {
    std::mutex            mutex;
    LatencyRecord         records[kMaxTrackedSegments];
    uint32_t              count;

    /** 0 = Normal mode (800ms budget), non-zero = Throttle mode (1200ms) */
    std::atomic<int>      throttle_mode;
};

/* ─── Internal helpers ────────────────────────────────────────────────── */

/**
 * Find or create a record for the given segment_id.
 * Must be called with tracker->mutex held.
 *
 * @return Pointer to the record, or NULL if buffer is full and segment
 *         not found (evicts oldest in that case).
 */
static LatencyRecord* find_or_create_record(LatencyTracker* tracker,
                                            uint32_t segment_id) {
    // Search existing records.
    for (uint32_t i = 0; i < tracker->count; ++i) {
        if (tracker->records[i].segment_id == segment_id) {
            return &tracker->records[i];
        }
    }

    // Not found — create new entry.
    if (tracker->count < kMaxTrackedSegments) {
        LatencyRecord* rec = &tracker->records[tracker->count];
        memset(rec, 0, sizeof(LatencyRecord));
        rec->segment_id = segment_id;
        tracker->count++;
        return rec;
    }

    // Buffer full — evict oldest (index 0) and shift.
    memmove(&tracker->records[0], &tracker->records[1],
            (kMaxTrackedSegments - 1) * sizeof(LatencyRecord));
    tracker->count = kMaxTrackedSegments;

    LatencyRecord* rec = &tracker->records[kMaxTrackedSegments - 1];
    memset(rec, 0, sizeof(LatencyRecord));
    rec->segment_id = segment_id;
    return rec;
}

/**
 * Find an existing record for the given segment_id.
 * Must be called with tracker->mutex held.
 *
 * @return Pointer to the record, or NULL if not found.
 */
static LatencyRecord* find_record(LatencyTracker* tracker, uint32_t segment_id) {
    for (uint32_t i = 0; i < tracker->count; ++i) {
        if (tracker->records[i].segment_id == segment_id) {
            return &tracker->records[i];
        }
    }
    return nullptr;
}

static const LatencyRecord* find_record_const(const LatencyTracker* tracker,
                                              uint32_t segment_id) {
    for (uint32_t i = 0; i < tracker->count; ++i) {
        if (tracker->records[i].segment_id == segment_id) {
            return &tracker->records[i];
        }
    }
    return nullptr;
}

/**
 * Check if a stage's latency exceeds its budget and report if so.
 */
static void check_and_report(const char* stage, uint64_t actual_ms, uint32_t budget_ms) {
    if (actual_ms > budget_ms) {
        native_port_post_latency_warning(stage,
                                         static_cast<int32_t>(actual_ms),
                                         static_cast<int32_t>(budget_ms));
    }
}

/* ─── Public API ─────────────────────────────────────────────────────────── */

LatencyTracker* latency_tracker_create(void) {
    auto* tracker = static_cast<LatencyTracker*>(calloc(1, sizeof(LatencyTracker)));
    if (!tracker) return nullptr;

    new (&tracker->mutex) std::mutex();
    new (&tracker->throttle_mode) std::atomic<int>(0);
    tracker->count = 0;

    return tracker;
}

void latency_tracker_destroy(LatencyTracker* tracker) {
    if (!tracker) return;

    tracker->mutex.~mutex();
    tracker->throttle_mode.~atomic<int>();
    free(tracker);
}

void latency_tracker_set_thermal_mode(LatencyTracker* tracker, int throttle) {
    if (!tracker) return;
    tracker->throttle_mode.store(throttle, std::memory_order_release);
}

void latency_tracker_on_segment_lock(LatencyTracker* tracker,
                                     uint32_t segment_id,
                                     uint64_t timestamp_ms) {
    if (!tracker) return;
    std::lock_guard<std::mutex> lock(tracker->mutex);

    LatencyRecord* rec = find_or_create_record(tracker, segment_id);
    if (rec) {
        rec->segment_lock_ms = timestamp_ms;
    }
}

void latency_tracker_on_asr_start(LatencyTracker* tracker,
                                  uint32_t segment_id,
                                  uint64_t timestamp_ms) {
    if (!tracker) return;
    std::lock_guard<std::mutex> lock(tracker->mutex);

    LatencyRecord* rec = find_record(tracker, segment_id);
    if (rec) {
        rec->asr_start_ms = timestamp_ms;
    }
}

void latency_tracker_on_asr_end(LatencyTracker* tracker,
                                uint32_t segment_id,
                                uint64_t timestamp_ms) {
    if (!tracker) return;
    std::lock_guard<std::mutex> lock(tracker->mutex);

    LatencyRecord* rec = find_record(tracker, segment_id);
    if (rec) {
        rec->asr_end_ms = timestamp_ms;

        // Check ASR stage latency: time from ASR start to ASR end.
        if (rec->asr_start_ms > 0) {
            uint64_t asr_latency = timestamp_ms - rec->asr_start_ms;
            check_and_report(kStageASR, asr_latency, LATENCY_BUDGET_ASR_MS);
        }
    }
}

void latency_tracker_on_llm_start(LatencyTracker* tracker,
                                  uint32_t segment_id,
                                  uint64_t timestamp_ms) {
    if (!tracker) return;
    std::lock_guard<std::mutex> lock(tracker->mutex);

    LatencyRecord* rec = find_record(tracker, segment_id);
    if (rec) {
        rec->llm_start_ms = timestamp_ms;
    }
}

void latency_tracker_on_llm_first_token(LatencyTracker* tracker,
                                        uint32_t segment_id,
                                        uint64_t timestamp_ms) {
    if (!tracker) return;
    std::lock_guard<std::mutex> lock(tracker->mutex);

    LatencyRecord* rec = find_record(tracker, segment_id);
    if (rec) {
        rec->llm_first_token_ms = timestamp_ms;

        // Check LLM stage latency: time from LLM start to first token.
        if (rec->llm_start_ms > 0) {
            uint64_t llm_latency = timestamp_ms - rec->llm_start_ms;
            check_and_report(kStageLLM, llm_latency, LATENCY_BUDGET_LLM_MS);
        }
    }
}

void latency_tracker_on_tts_start(LatencyTracker* tracker,
                                  uint32_t segment_id,
                                  uint64_t timestamp_ms) {
    if (!tracker) return;
    std::lock_guard<std::mutex> lock(tracker->mutex);

    LatencyRecord* rec = find_record(tracker, segment_id);
    if (rec) {
        rec->tts_start_ms = timestamp_ms;
    }
}

void latency_tracker_on_tts_first_audio(LatencyTracker* tracker,
                                        uint32_t segment_id,
                                        uint64_t timestamp_ms) {
    if (!tracker) return;
    std::lock_guard<std::mutex> lock(tracker->mutex);

    LatencyRecord* rec = find_record(tracker, segment_id);
    if (rec) {
        rec->tts_first_audio_ms = timestamp_ms;

        // Check TTS stage latency: time from TTS start to first audio.
        if (rec->tts_start_ms > 0) {
            uint64_t tts_latency = timestamp_ms - rec->tts_start_ms;
            check_and_report(kStageTTS, tts_latency, LATENCY_BUDGET_TTS_MS);
        }

        // Check E2E latency: time from segment lock to TTS first audio.
        if (rec->segment_lock_ms > 0) {
            uint64_t e2e_latency = timestamp_ms - rec->segment_lock_ms;
            int throttle = tracker->throttle_mode.load(std::memory_order_acquire);
            uint32_t e2e_budget = throttle
                ? LATENCY_BUDGET_E2E_THROTTLE_MS
                : LATENCY_BUDGET_E2E_NORMAL_MS;

            check_and_report(kStageE2E, e2e_latency, e2e_budget);
        }
    }
}

bool latency_tracker_get_record(const LatencyTracker* tracker,
                                uint32_t segment_id,
                                LatencyRecord* out_record) {
    if (!tracker || !out_record) return false;

    // const_cast is safe here — we only read under lock.
    auto* mutable_tracker = const_cast<LatencyTracker*>(tracker);
    std::lock_guard<std::mutex> lock(mutable_tracker->mutex);

    const LatencyRecord* rec = find_record_const(tracker, segment_id);
    if (rec) {
        memcpy(out_record, rec, sizeof(LatencyRecord));
        return true;
    }
    return false;
}
