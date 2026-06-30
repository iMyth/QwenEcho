/**
 * Sentence Segmenter — VAD + Sentence Boundary Detection.
 *
 * Classifies audio frames as speech/non-speech using an energy-based VAD
 * (simulating FSMN-VAD) and determines sentence boundaries via a state machine.
 *
 * State Machine:
 *   Idle → Accumulating (on speech onset)
 *   Accumulating → Locking (on 400ms silence, punctuation, or 15s force-lock)
 *   Locking → Idle (segment dispatched)
 *
 * Lock conditions:
 * - 400ms continuous silence after ≥200ms speech
 * - Sentence-ending punctuation detected in ASR output
 * - 15s continuous speech without any boundary
 *
 * Minimum segment: 200ms of speech audio required before any lock.
 *
 * Validates: Requirements 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7
 */

#ifndef SENTENCE_SEGMENTER_H
#define SENTENCE_SEGMENTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Segmenter state machine states.
 */
typedef enum {
    SEG_STATE_IDLE = 0,         /* Waiting for speech onset */
    SEG_STATE_ACCUMULATING,     /* Accumulating speech audio */
    SEG_STATE_LOCKING,          /* Segment locked, dispatching */
} SegmenterState;

/**
 * A locked audio segment ready for ASR processing.
 */
typedef struct {
    const int16_t* audio_data;  /* Pointer to segment audio samples */
    uint32_t sample_count;      /* Number of samples in the segment */
    uint32_t segment_id;        /* Auto-incrementing segment identifier */
    uint8_t speaker_id;         /* Speaker identifier (0 or 1) */
    uint64_t timestamp_ms;      /* Timestamp when segment was locked (ms) */
} LockedSegment;

/**
 * Callback invoked when a segment is locked and ready for ASR.
 *
 * @param segment  Pointer to the locked segment data (valid only during callback)
 * @param user     User-provided context pointer
 */
typedef void (*segment_locked_callback_t)(const LockedSegment* segment, void* user);

/**
 * Opaque sentence segmenter handle.
 */
typedef struct SentenceSegmenter SentenceSegmenter;

/**
 * Create a new sentence segmenter instance.
 *
 * @param sample_rate  Audio sample rate in Hz (typically 16000)
 * @param callback     Function to invoke when a segment is locked
 * @param user_data    Opaque pointer passed to the callback
 * @return Pointer to created segmenter, or NULL on allocation failure
 */
SentenceSegmenter* sentence_segmenter_create(uint32_t sample_rate,
                                             segment_locked_callback_t callback,
                                             void* user_data);

/**
 * Configure segmenter timing thresholds.
 *
 * @param seg                    Segmenter instance
 * @param silence_threshold_ms   Silence duration to trigger lock (default: 400)
 * @param min_speech_ms          Minimum speech duration before locking (default: 200)
 * @param max_segment_ms         Maximum segment duration before force-lock (default: 15000)
 */
void sentence_segmenter_configure(SentenceSegmenter* seg,
                                  uint32_t silence_threshold_ms,
                                  uint32_t min_speech_ms,
                                  uint32_t max_segment_ms);

/**
 * Feed audio samples to the segmenter for VAD processing.
 *
 * Processes audio in 10ms frames (160 samples at 16kHz). For each frame,
 * performs energy-based VAD classification and drives the state machine.
 *
 * @param seg         Segmenter instance
 * @param samples     Pointer to PCM 16-bit audio samples
 * @param count       Number of samples
 * @param speaker_id  Speaker identifier for the current audio
 */
void sentence_segmenter_feed_audio(SentenceSegmenter* seg,
                                   const int16_t* samples,
                                   uint32_t count,
                                   uint8_t speaker_id);

/**
 * Notify the segmenter that sentence-ending punctuation was detected by ASR.
 *
 * If in Accumulating state with sufficient speech (≥min_speech_ms),
 * forces an immediate segment lock.
 *
 * @param seg  Segmenter instance
 */
void sentence_segmenter_notify_punctuation(SentenceSegmenter* seg);

/**
 * Query the current state of the segmenter.
 *
 * @param seg  Segmenter instance
 * @return Current segmenter state
 */
SegmenterState sentence_segmenter_get_state(const SentenceSegmenter* seg);

/**
 * Reset the segmenter to Idle state, discarding any accumulated audio.
 *
 * @param seg  Segmenter instance
 */
void sentence_segmenter_reset(SentenceSegmenter* seg);

/**
 * Destroy the segmenter and free all resources.
 *
 * @param seg  Segmenter instance (may be NULL)
 */
void sentence_segmenter_destroy(SentenceSegmenter* seg);

#ifdef __cplusplus
}
#endif

#endif /* SENTENCE_SEGMENTER_H */
