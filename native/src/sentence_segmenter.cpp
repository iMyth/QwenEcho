/**
 * Sentence Segmenter — VAD + Sentence Boundary Detection Implementation.
 *
 * Uses an energy-based VAD (simulating FSMN-VAD per-frame classification)
 * to drive a state machine that segments audio into translatable chunks.
 *
 * Frame processing:
 * - Frame size: 10ms (160 samples at 16kHz)
 * - Per-frame classification must complete within 30ms (energy threshold is O(n))
 * - Speech detection: mean absolute amplitude > configurable threshold
 *
 * State machine:
 *   Idle → Accumulating: when speech onset detected
 *   Accumulating → Locking: on 400ms silence, punctuation, or 15s force-lock
 *   Locking → Idle: after segment dispatched via callback
 *
 * Validates: Requirements 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7
 */

#include "sentence_segmenter.h"

#include <cstdlib>
#include <cstring>
#include <vector>
#include <cstdint>

/* --------------------------------------------------------------------------
 * Internal VAD energy threshold.
 *
 * In production this would be replaced by FSMN-VAD model inference.
 * For now, we use a simple energy-based detector: compute the mean absolute
 * value of samples in a frame and compare against a threshold.
 * A typical threshold of 300 works well for 16-bit PCM speech vs silence.
 * -------------------------------------------------------------------------- */
static constexpr int32_t VAD_ENERGY_THRESHOLD = 300;

/* Initial capacity for the audio accumulation buffer (in samples) */
static constexpr uint32_t INITIAL_BUFFER_CAPACITY = 16000 * 2; /* 2 seconds */

/**
 * Internal sentence segmenter structure.
 */
struct SentenceSegmenter {
    /* Configuration */
    uint32_t sample_rate;
    uint32_t frame_size;              /* Samples per frame (10ms) */
    uint32_t silence_threshold_ms;    /* Silence duration to trigger lock */
    uint32_t min_speech_ms;           /* Minimum speech before locking */
    uint32_t max_segment_ms;          /* Maximum segment before force-lock */

    /* Derived thresholds in frames */
    uint32_t silence_threshold_frames;
    uint32_t min_speech_frames;
    uint32_t max_segment_frames;

    /* State */
    SegmenterState state;
    uint32_t silence_frame_count;     /* Consecutive silence frames */
    uint32_t speech_frame_count;      /* Total speech frames in current segment */
    uint32_t total_frame_count;       /* Total frames in current segment */

    /* Audio accumulation buffer */
    std::vector<int16_t> audio_buffer;

    /* Segment tracking */
    uint32_t next_segment_id;
    uint8_t current_speaker_id;

    /* Callback */
    segment_locked_callback_t callback;
    void* user_data;

    /* Leftover samples that don't fill a complete frame */
    std::vector<int16_t> frame_remainder;
};

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/**
 * Compute the mean absolute energy of a frame of samples.
 * Returns true if the frame is classified as speech.
 */
static bool vad_classify_frame(const int16_t* samples, uint32_t count) {
    if (count == 0) return false;

    int64_t sum = 0;
    for (uint32_t i = 0; i < count; ++i) {
        int32_t val = samples[i];
        sum += (val < 0) ? -val : val;
    }

    int32_t mean_energy = static_cast<int32_t>(sum / count);
    return mean_energy > VAD_ENERGY_THRESHOLD;
}

/**
 * Recalculate frame-based thresholds from millisecond configuration.
 */
static void recalculate_thresholds(SentenceSegmenter* seg) {
    /* frames per ms = sample_rate / (1000 * frame_size)
     * e.g., 16000 / (1000 * 160) = 0.1 frames/ms → 1 frame per 10ms */
    uint32_t ms_per_frame = (seg->frame_size * 1000) / seg->sample_rate;
    if (ms_per_frame == 0) ms_per_frame = 1;

    seg->silence_threshold_frames = seg->silence_threshold_ms / ms_per_frame;
    seg->min_speech_frames = seg->min_speech_ms / ms_per_frame;
    seg->max_segment_frames = seg->max_segment_ms / ms_per_frame;
}

/**
 * Lock the current segment: invoke callback, reset to Idle.
 */
static void lock_and_dispatch(SentenceSegmenter* seg) {
    seg->state = SEG_STATE_LOCKING;

    if (seg->callback && !seg->audio_buffer.empty()) {
        LockedSegment locked;
        locked.audio_data = seg->audio_buffer.data();
        locked.sample_count = static_cast<uint32_t>(seg->audio_buffer.size());
        locked.segment_id = seg->next_segment_id;
        locked.speaker_id = seg->current_speaker_id;
        /* Timestamp: approximate from total frames processed */
        uint32_t ms_per_frame = (seg->frame_size * 1000) / seg->sample_rate;
        locked.timestamp_ms = static_cast<uint64_t>(seg->total_frame_count) * ms_per_frame;

        seg->callback(&locked, seg->user_data);
    }

    /* Advance segment ID */
    seg->next_segment_id++;

    /* Reset to Idle */
    seg->state = SEG_STATE_IDLE;
    seg->audio_buffer.clear();
    seg->silence_frame_count = 0;
    seg->speech_frame_count = 0;
    seg->total_frame_count = 0;
}

/**
 * Process a single frame through the state machine.
 */
static void process_frame(SentenceSegmenter* seg, const int16_t* frame, uint32_t frame_len) {
    bool is_speech = vad_classify_frame(frame, frame_len);

    switch (seg->state) {
        case SEG_STATE_IDLE:
            if (is_speech) {
                /* Speech onset: transition to Accumulating */
                seg->state = SEG_STATE_ACCUMULATING;
                seg->silence_frame_count = 0;
                seg->speech_frame_count = 1;
                seg->total_frame_count = 1;
                /* Begin accumulating audio */
                seg->audio_buffer.insert(seg->audio_buffer.end(), frame, frame + frame_len);
            }
            /* If non-speech in Idle, stay in Idle — do nothing */
            break;

        case SEG_STATE_ACCUMULATING:
            /* Always accumulate the audio data */
            seg->audio_buffer.insert(seg->audio_buffer.end(), frame, frame + frame_len);
            seg->total_frame_count++;

            if (is_speech) {
                seg->speech_frame_count++;
                seg->silence_frame_count = 0;
            } else {
                seg->silence_frame_count++;
            }

            /* Check force-lock at max segment duration */
            if (seg->total_frame_count >= seg->max_segment_frames) {
                /* Force-lock only if minimum speech is met */
                if (seg->speech_frame_count >= seg->min_speech_frames) {
                    lock_and_dispatch(seg);
                    return;
                }
            }

            /* Check silence-based lock */
            if (seg->silence_frame_count >= seg->silence_threshold_frames) {
                /* Lock only if minimum speech duration met */
                if (seg->speech_frame_count >= seg->min_speech_frames) {
                    lock_and_dispatch(seg);
                    return;
                }
            }
            break;

        case SEG_STATE_LOCKING:
            /* Should not receive frames in Locking state (transient).
             * If we do, just buffer them for the next segment. */
            break;
    }
}

/* --------------------------------------------------------------------------
 * Public API implementation
 * -------------------------------------------------------------------------- */

extern "C" {

SentenceSegmenter* sentence_segmenter_create(uint32_t sample_rate,
                                             segment_locked_callback_t callback,
                                             void* user_data) {
    if (sample_rate == 0) return nullptr;

    SentenceSegmenter* seg = new (std::nothrow) SentenceSegmenter();
    if (!seg) return nullptr;

    seg->sample_rate = sample_rate;
    seg->frame_size = sample_rate / 100; /* 10ms frame: 16000/100 = 160 samples */
    seg->callback = callback;
    seg->user_data = user_data;

    /* Default thresholds */
    seg->silence_threshold_ms = 400;
    seg->min_speech_ms = 200;
    seg->max_segment_ms = 15000;
    recalculate_thresholds(seg);

    /* Initial state */
    seg->state = SEG_STATE_IDLE;
    seg->silence_frame_count = 0;
    seg->speech_frame_count = 0;
    seg->total_frame_count = 0;
    seg->next_segment_id = 0;
    seg->current_speaker_id = 0;

    /* Pre-allocate buffer */
    seg->audio_buffer.reserve(INITIAL_BUFFER_CAPACITY);

    return seg;
}

void sentence_segmenter_configure(SentenceSegmenter* seg,
                                  uint32_t silence_threshold_ms,
                                  uint32_t min_speech_ms,
                                  uint32_t max_segment_ms) {
    if (!seg) return;

    seg->silence_threshold_ms = silence_threshold_ms;
    seg->min_speech_ms = min_speech_ms;
    seg->max_segment_ms = max_segment_ms;
    recalculate_thresholds(seg);
}

void sentence_segmenter_feed_audio(SentenceSegmenter* seg,
                                   const int16_t* samples,
                                   uint32_t count,
                                   uint8_t speaker_id) {
    if (!seg || !samples || count == 0) return;

    seg->current_speaker_id = speaker_id;

    /* Combine any leftover samples from previous call with new samples */
    uint32_t offset = 0;

    /* If we have leftover from previous feed, prepend them */
    if (!seg->frame_remainder.empty()) {
        uint32_t needed = seg->frame_size - static_cast<uint32_t>(seg->frame_remainder.size());
        uint32_t available = (count < needed) ? count : needed;

        seg->frame_remainder.insert(seg->frame_remainder.end(),
                                    samples, samples + available);
        offset = available;

        /* If we now have a full frame, process it */
        if (seg->frame_remainder.size() >= seg->frame_size) {
            process_frame(seg, seg->frame_remainder.data(), seg->frame_size);
            seg->frame_remainder.clear();
        } else {
            /* Still not enough for a full frame */
            return;
        }
    }

    /* Process complete frames from the remaining input */
    while (offset + seg->frame_size <= count) {
        process_frame(seg, samples + offset, seg->frame_size);
        offset += seg->frame_size;

        /* If the state was reset to Idle during lock_and_dispatch,
         * continue processing remaining frames normally */
    }

    /* Store any leftover samples for next call */
    if (offset < count) {
        seg->frame_remainder.assign(samples + offset, samples + count);
    }
}

void sentence_segmenter_notify_punctuation(SentenceSegmenter* seg) {
    if (!seg) return;

    /* Punctuation lock only makes sense when accumulating */
    if (seg->state != SEG_STATE_ACCUMULATING) return;

    /* Only lock if minimum speech duration is met */
    if (seg->speech_frame_count >= seg->min_speech_frames) {
        lock_and_dispatch(seg);
    }
}

SegmenterState sentence_segmenter_get_state(const SentenceSegmenter* seg) {
    if (!seg) return SEG_STATE_IDLE;
    return seg->state;
}

void sentence_segmenter_reset(SentenceSegmenter* seg) {
    if (!seg) return;

    seg->state = SEG_STATE_IDLE;
    seg->audio_buffer.clear();
    seg->frame_remainder.clear();
    seg->silence_frame_count = 0;
    seg->speech_frame_count = 0;
    seg->total_frame_count = 0;
}

void sentence_segmenter_destroy(SentenceSegmenter* seg) {
    if (!seg) return;
    delete seg;
}

} /* extern "C" */
