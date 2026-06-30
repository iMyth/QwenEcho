/**
 * @file latency_tracker.h
 * @brief E2E Latency Tracker — measures per-segment pipeline latency and
 *        reports SLA violations via MSG_LATENCY_WARNING.
 *
 * Tracks timestamps at each stage boundary for a segment flowing through
 * the pipeline:
 *   Segment Lock → ASR Start → ASR End → LLM Start → LLM First-Token →
 *   TTS Start → TTS First-Audio
 *
 * E2E latency is computed as: TTS first-audio time - Segment lock time.
 *
 * Latency budgets:
 *   - Normal mode:   ASR ≤200ms + LLM ≤450ms + TTS ≤100ms = ≤800ms total
 *   - Throttle mode: Total E2E ≤1200ms
 *
 * When a stage exceeds its individual budget OR the E2E exceeds the total
 * budget for the current thermal mode, a MSG_LATENCY_WARNING is posted
 * identifying the offending stage, actual latency, and budget.
 *
 * Validates: Requirements 8.1, 8.2, 8.3, 8.4, 8.5
 */

#ifndef LATENCY_TRACKER_H
#define LATENCY_TRACKER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Per-stage latency budgets (milliseconds) ─────────────────────────── */

/** ASR stage budget: first-character latency ≤200ms */
#define LATENCY_BUDGET_ASR_MS       200

/** LLM stage budget: first-token latency ≤450ms */
#define LATENCY_BUDGET_LLM_MS       450

/** TTS stage budget: time to first audio ≤100ms */
#define LATENCY_BUDGET_TTS_MS       100

/** Total E2E budget in Normal thermal mode */
#define LATENCY_BUDGET_E2E_NORMAL_MS   800

/** Total E2E budget in Throttle thermal mode */
#define LATENCY_BUDGET_E2E_THROTTLE_MS 1200

/* ─── Latency record for a single segment ─────────────────────────────── */

/**
 * Per-segment latency timestamps (milliseconds since arbitrary epoch).
 * Each field is set to 0 until the corresponding event occurs.
 */
typedef struct {
    uint32_t segment_id;

    /** Timestamp when the sentence segmenter locked this segment */
    uint64_t segment_lock_ms;

    /** Timestamp when ASR began processing this segment */
    uint64_t asr_start_ms;

    /** Timestamp when ASR produced the first character / confirmed text */
    uint64_t asr_end_ms;

    /** Timestamp when LLM began processing this segment */
    uint64_t llm_start_ms;

    /** Timestamp when LLM produced the first translated token */
    uint64_t llm_first_token_ms;

    /** Timestamp when TTS began synthesis for this segment */
    uint64_t tts_start_ms;

    /** Timestamp when TTS produced the first audio sample */
    uint64_t tts_first_audio_ms;
} LatencyRecord;

/* ─── Latency Tracker handle ──────────────────────────────────────────── */

/**
 * Opaque Latency Tracker handle.
 *
 * Maintains a circular buffer of recent LatencyRecords and checks each
 * stage transition against the SLA budget for the current thermal mode.
 */
typedef struct LatencyTracker LatencyTracker;

/**
 * Create a Latency Tracker instance.
 *
 * @return Pointer to the new LatencyTracker, or NULL on allocation failure.
 */
LatencyTracker* latency_tracker_create(void);

/**
 * Destroy the Latency Tracker and release all resources.
 * NULL is safely ignored.
 *
 * @param tracker  LatencyTracker instance to destroy.
 */
void latency_tracker_destroy(LatencyTracker* tracker);

/* ─── Thermal mode configuration ──────────────────────────────────────── */

/**
 * Update the current thermal mode.
 *
 * This determines which E2E budget is used for SLA checking:
 *   0 = Normal  (800ms budget)
 *   1 = Throttle (1200ms budget)
 *
 * @param tracker  LatencyTracker instance.
 * @param throttle Non-zero for Throttle mode, 0 for Normal mode.
 */
void latency_tracker_set_thermal_mode(LatencyTracker* tracker, int throttle);

/* ─── Stage boundary event recording ──────────────────────────────────── */

/**
 * Record when a segment is locked by the sentence segmenter.
 * Creates a new LatencyRecord entry for this segment.
 *
 * @param tracker     LatencyTracker instance.
 * @param segment_id  Unique segment identifier.
 * @param timestamp_ms  Current time in milliseconds.
 */
void latency_tracker_on_segment_lock(LatencyTracker* tracker,
                                     uint32_t segment_id,
                                     uint64_t timestamp_ms);

/**
 * Record when ASR begins processing a segment.
 *
 * @param tracker     LatencyTracker instance.
 * @param segment_id  Segment identifier.
 * @param timestamp_ms  Current time in milliseconds.
 */
void latency_tracker_on_asr_start(LatencyTracker* tracker,
                                  uint32_t segment_id,
                                  uint64_t timestamp_ms);

/**
 * Record when ASR produces confirmed text for a segment.
 * Checks ASR stage budget and reports MSG_LATENCY_WARNING if exceeded.
 *
 * @param tracker     LatencyTracker instance.
 * @param segment_id  Segment identifier.
 * @param timestamp_ms  Current time in milliseconds.
 */
void latency_tracker_on_asr_end(LatencyTracker* tracker,
                                uint32_t segment_id,
                                uint64_t timestamp_ms);

/**
 * Record when LLM begins processing a segment.
 *
 * @param tracker     LatencyTracker instance.
 * @param segment_id  Segment identifier.
 * @param timestamp_ms  Current time in milliseconds.
 */
void latency_tracker_on_llm_start(LatencyTracker* tracker,
                                  uint32_t segment_id,
                                  uint64_t timestamp_ms);

/**
 * Record when LLM produces the first translated token for a segment.
 * Checks LLM stage budget and reports MSG_LATENCY_WARNING if exceeded.
 *
 * @param tracker     LatencyTracker instance.
 * @param segment_id  Segment identifier.
 * @param timestamp_ms  Current time in milliseconds.
 */
void latency_tracker_on_llm_first_token(LatencyTracker* tracker,
                                        uint32_t segment_id,
                                        uint64_t timestamp_ms);

/**
 * Record when TTS begins synthesis for a segment.
 *
 * @param tracker     LatencyTracker instance.
 * @param segment_id  Segment identifier.
 * @param timestamp_ms  Current time in milliseconds.
 */
void latency_tracker_on_tts_start(LatencyTracker* tracker,
                                  uint32_t segment_id,
                                  uint64_t timestamp_ms);

/**
 * Record when TTS produces the first audio sample for a segment.
 * Checks TTS stage budget and E2E budget, reports MSG_LATENCY_WARNING
 * if either is exceeded.
 *
 * @param tracker     LatencyTracker instance.
 * @param segment_id  Segment identifier.
 * @param timestamp_ms  Current time in milliseconds.
 */
void latency_tracker_on_tts_first_audio(LatencyTracker* tracker,
                                        uint32_t segment_id,
                                        uint64_t timestamp_ms);

/* ─── Query ───────────────────────────────────────────────────────────── */

/**
 * Get the most recent LatencyRecord for a given segment.
 *
 * @param tracker     LatencyTracker instance.
 * @param segment_id  Segment identifier to look up.
 * @param out_record  Output pointer for the record (copied out).
 * @return true if the segment was found, false otherwise.
 */
bool latency_tracker_get_record(const LatencyTracker* tracker,
                                uint32_t segment_id,
                                LatencyRecord* out_record);

#ifdef __cplusplus
}
#endif

#endif /* LATENCY_TRACKER_H */
