/**
 * Unit tests for the Sentence Segmenter (VAD + Sentence Boundary Detection).
 *
 * Tests the state machine transitions, lock conditions, and edge cases:
 * - Idle → Accumulating on speech onset
 * - Lock on 400ms silence after ≥200ms speech
 * - Lock on punctuation notification
 * - Force-lock at 15s continuous speech
 * - Minimum 200ms speech requirement before locking
 * - Immediate new segment after lock
 *
 * Validates: Requirements 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7
 */

#include <rapidcheck.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "sentence_segmenter.h"

/* --------------------------------------------------------------------------
 * Test helpers
 * -------------------------------------------------------------------------- */

static constexpr uint32_t SAMPLE_RATE = 16000;
static constexpr uint32_t FRAME_SIZE = 160;  /* 10ms at 16kHz */
static constexpr uint32_t FRAMES_PER_SEC = 100;

/**
 * Generate a frame of "speech" audio (high energy, above VAD threshold).
 * Uses a sawtooth pattern with amplitude ~1000 to clearly exceed the
 * energy threshold of 300.
 */
static void generate_speech_frame(int16_t* frame, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        frame[i] = static_cast<int16_t>(800 + (i % 400));
    }
}

/**
 * Generate a frame of "silence" audio (low energy, below VAD threshold).
 */
static void generate_silence_frame(int16_t* frame, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        frame[i] = static_cast<int16_t>((i % 3) - 1); /* Near-zero noise */
    }
}

/**
 * Feed N frames of speech to the segmenter.
 */
static void feed_speech_frames(SentenceSegmenter* seg, uint32_t num_frames, uint8_t speaker = 0) {
    int16_t frame[FRAME_SIZE];
    generate_speech_frame(frame, FRAME_SIZE);
    for (uint32_t i = 0; i < num_frames; ++i) {
        sentence_segmenter_feed_audio(seg, frame, FRAME_SIZE, speaker);
    }
}

/**
 * Feed N frames of silence to the segmenter.
 */
static void feed_silence_frames(SentenceSegmenter* seg, uint32_t num_frames, uint8_t speaker = 0) {
    int16_t frame[FRAME_SIZE];
    generate_silence_frame(frame, FRAME_SIZE);
    for (uint32_t i = 0; i < num_frames; ++i) {
        sentence_segmenter_feed_audio(seg, frame, FRAME_SIZE, speaker);
    }
}

/* Callback tracking */
struct CallbackState {
    uint32_t call_count = 0;
    uint32_t last_sample_count = 0;
    uint32_t last_segment_id = 0;
    uint8_t last_speaker_id = 0;
    std::vector<uint32_t> segment_sample_counts;
};

static void test_callback(const LockedSegment* segment, void* user) {
    auto* state = static_cast<CallbackState*>(user);
    state->call_count++;
    state->last_sample_count = segment->sample_count;
    state->last_segment_id = segment->segment_id;
    state->last_speaker_id = segment->speaker_id;
    state->segment_sample_counts.push_back(segment->sample_count);
}

/* --------------------------------------------------------------------------
 * Unit Tests
 * -------------------------------------------------------------------------- */

int main() {
    /* Test 1: Initial state is Idle */
    rc::check("Initial state is Idle", []() {
        CallbackState cb_state;
        auto* seg = sentence_segmenter_create(SAMPLE_RATE, test_callback, &cb_state);
        RC_ASSERT(seg != nullptr);
        RC_ASSERT(sentence_segmenter_get_state(seg) == SEG_STATE_IDLE);
        RC_ASSERT(cb_state.call_count == 0);
        sentence_segmenter_destroy(seg);
    });

    /* Test 2: Speech onset transitions Idle → Accumulating */
    rc::check("Speech onset transitions to Accumulating", []() {
        CallbackState cb_state;
        auto* seg = sentence_segmenter_create(SAMPLE_RATE, test_callback, &cb_state);

        /* Feed one frame of speech */
        feed_speech_frames(seg, 1);

        RC_ASSERT(sentence_segmenter_get_state(seg) == SEG_STATE_ACCUMULATING);
        RC_ASSERT(cb_state.call_count == 0); /* No lock yet */

        sentence_segmenter_destroy(seg);
    });

    /* Test 3: Silence alone does not transition from Idle */
    rc::check("Silence alone stays in Idle", []() {
        CallbackState cb_state;
        auto* seg = sentence_segmenter_create(SAMPLE_RATE, test_callback, &cb_state);

        feed_silence_frames(seg, 100); /* 1 second of silence */

        RC_ASSERT(sentence_segmenter_get_state(seg) == SEG_STATE_IDLE);
        RC_ASSERT(cb_state.call_count == 0);

        sentence_segmenter_destroy(seg);
    });

    /* Test 4: Lock on 400ms silence after ≥200ms speech */
    rc::check("Lock on 400ms silence after sufficient speech", []() {
        CallbackState cb_state;
        auto* seg = sentence_segmenter_create(SAMPLE_RATE, test_callback, &cb_state);

        /* Feed 300ms of speech (30 frames) — exceeds 200ms minimum */
        feed_speech_frames(seg, 30);
        RC_ASSERT(sentence_segmenter_get_state(seg) == SEG_STATE_ACCUMULATING);

        /* Feed 400ms of silence (40 frames) — triggers lock */
        feed_silence_frames(seg, 40);

        RC_ASSERT(sentence_segmenter_get_state(seg) == SEG_STATE_IDLE);
        RC_ASSERT(cb_state.call_count == 1);
        /* Total samples: 30 speech + 40 silence = 70 frames * 160 = 11200 */
        RC_ASSERT(cb_state.last_sample_count == 70 * FRAME_SIZE);
        RC_ASSERT(cb_state.last_segment_id == 0);

        sentence_segmenter_destroy(seg);
    });

    /* Test 5: No lock if speech < 200ms even with 400ms silence */
    rc::check("No lock if speech below minimum duration", []() {
        CallbackState cb_state;
        auto* seg = sentence_segmenter_create(SAMPLE_RATE, test_callback, &cb_state);

        /* Feed 150ms of speech (15 frames) — below 200ms minimum */
        feed_speech_frames(seg, 15);

        /* Feed 500ms of silence (50 frames) — would normally trigger lock */
        feed_silence_frames(seg, 50);

        /* Should NOT have locked since min_speech not met */
        RC_ASSERT(cb_state.call_count == 0);
        RC_ASSERT(sentence_segmenter_get_state(seg) == SEG_STATE_ACCUMULATING);

        sentence_segmenter_destroy(seg);
    });

    /* Test 6: Punctuation notification triggers lock with sufficient speech */
    rc::check("Punctuation triggers lock with sufficient speech", []() {
        CallbackState cb_state;
        auto* seg = sentence_segmenter_create(SAMPLE_RATE, test_callback, &cb_state);

        /* Feed 250ms of speech (25 frames) */
        feed_speech_frames(seg, 25);
        RC_ASSERT(sentence_segmenter_get_state(seg) == SEG_STATE_ACCUMULATING);

        /* Notify punctuation */
        sentence_segmenter_notify_punctuation(seg);

        RC_ASSERT(sentence_segmenter_get_state(seg) == SEG_STATE_IDLE);
        RC_ASSERT(cb_state.call_count == 1);
        RC_ASSERT(cb_state.last_sample_count == 25 * FRAME_SIZE);

        sentence_segmenter_destroy(seg);
    });

    /* Test 7: Punctuation does NOT lock if speech below minimum */
    rc::check("Punctuation does not lock if speech below minimum", []() {
        CallbackState cb_state;
        auto* seg = sentence_segmenter_create(SAMPLE_RATE, test_callback, &cb_state);

        /* Feed 100ms of speech (10 frames) — below 200ms minimum */
        feed_speech_frames(seg, 10);

        /* Notify punctuation */
        sentence_segmenter_notify_punctuation(seg);

        /* Should NOT lock */
        RC_ASSERT(cb_state.call_count == 0);
        RC_ASSERT(sentence_segmenter_get_state(seg) == SEG_STATE_ACCUMULATING);

        sentence_segmenter_destroy(seg);
    });

    /* Test 8: Force-lock at 15s continuous speech */
    rc::check("Force-lock at 15 seconds continuous speech", []() {
        CallbackState cb_state;
        auto* seg = sentence_segmenter_create(SAMPLE_RATE, test_callback, &cb_state);

        /* Feed exactly 15s of speech = 1500 frames */
        feed_speech_frames(seg, 1500);

        RC_ASSERT(sentence_segmenter_get_state(seg) == SEG_STATE_IDLE);
        RC_ASSERT(cb_state.call_count == 1);
        RC_ASSERT(cb_state.last_sample_count == 1500 * FRAME_SIZE);

        sentence_segmenter_destroy(seg);
    });

    /* Test 9: Segment ID auto-increments */
    rc::check("Segment ID auto-increments across locks", []() {
        CallbackState cb_state;
        auto* seg = sentence_segmenter_create(SAMPLE_RATE, test_callback, &cb_state);

        /* First segment: 300ms speech + 400ms silence */
        feed_speech_frames(seg, 30);
        feed_silence_frames(seg, 40);
        RC_ASSERT(cb_state.call_count == 1);
        RC_ASSERT(cb_state.last_segment_id == 0);

        /* Second segment: 300ms speech + punctuation */
        feed_speech_frames(seg, 30);
        sentence_segmenter_notify_punctuation(seg);
        RC_ASSERT(cb_state.call_count == 2);
        RC_ASSERT(cb_state.last_segment_id == 1);

        /* Third segment: another 300ms speech + 400ms silence */
        feed_speech_frames(seg, 30);
        feed_silence_frames(seg, 40);
        RC_ASSERT(cb_state.call_count == 3);
        RC_ASSERT(cb_state.last_segment_id == 2);

        sentence_segmenter_destroy(seg);
    });

    /* Test 10: New segment begins immediately after lock */
    rc::check("New segment accumulates immediately after lock", []() {
        CallbackState cb_state;
        auto* seg = sentence_segmenter_create(SAMPLE_RATE, test_callback, &cb_state);

        /* First segment: speech + silence to lock */
        feed_speech_frames(seg, 30);
        feed_silence_frames(seg, 40);
        RC_ASSERT(cb_state.call_count == 1);
        RC_ASSERT(sentence_segmenter_get_state(seg) == SEG_STATE_IDLE);

        /* New speech immediately starts new segment */
        feed_speech_frames(seg, 1);
        RC_ASSERT(sentence_segmenter_get_state(seg) == SEG_STATE_ACCUMULATING);

        sentence_segmenter_destroy(seg);
    });

    /* Test 11: Speaker ID is carried through to locked segment */
    rc::check("Speaker ID passed to locked segment", []() {
        CallbackState cb_state;
        auto* seg = sentence_segmenter_create(SAMPLE_RATE, test_callback, &cb_state);

        feed_speech_frames(seg, 30, /* speaker_id */ 1);
        sentence_segmenter_notify_punctuation(seg);

        RC_ASSERT(cb_state.call_count == 1);
        RC_ASSERT(cb_state.last_speaker_id == 1);

        sentence_segmenter_destroy(seg);
    });

    /* Test 12: Reset clears state back to Idle */
    rc::check("Reset returns to Idle and clears buffers", []() {
        CallbackState cb_state;
        auto* seg = sentence_segmenter_create(SAMPLE_RATE, test_callback, &cb_state);

        feed_speech_frames(seg, 30);
        RC_ASSERT(sentence_segmenter_get_state(seg) == SEG_STATE_ACCUMULATING);

        sentence_segmenter_reset(seg);
        RC_ASSERT(sentence_segmenter_get_state(seg) == SEG_STATE_IDLE);
        RC_ASSERT(cb_state.call_count == 0); /* No callback on reset */

        sentence_segmenter_destroy(seg);
    });

    /* Test 13: Configure thresholds */
    rc::check("Custom thresholds work correctly", []() {
        CallbackState cb_state;
        auto* seg = sentence_segmenter_create(SAMPLE_RATE, test_callback, &cb_state);

        /* Configure: 200ms silence, 100ms min speech, 5s max */
        sentence_segmenter_configure(seg, 200, 100, 5000);

        /* 120ms of speech (12 frames) — exceeds 100ms minimum */
        feed_speech_frames(seg, 12);

        /* 200ms of silence (20 frames) — triggers lock with new threshold */
        feed_silence_frames(seg, 20);

        RC_ASSERT(cb_state.call_count == 1);
        RC_ASSERT(sentence_segmenter_get_state(seg) == SEG_STATE_IDLE);

        sentence_segmenter_destroy(seg);
    });

    /* Test 14: NULL safety */
    rc::check("NULL parameter handling", []() {
        /* All functions should handle NULL gracefully */
        sentence_segmenter_feed_audio(nullptr, nullptr, 0, 0);
        sentence_segmenter_notify_punctuation(nullptr);
        RC_ASSERT(sentence_segmenter_get_state(nullptr) == SEG_STATE_IDLE);
        sentence_segmenter_reset(nullptr);
        sentence_segmenter_destroy(nullptr);

        /* Create with zero sample rate should fail */
        auto* seg = sentence_segmenter_create(0, nullptr, nullptr);
        RC_ASSERT(seg == nullptr);
    });

    /* Test 15: Partial frame handling across multiple feed calls */
    rc::check("Partial frames handled correctly across calls", []() {
        CallbackState cb_state;
        auto* seg = sentence_segmenter_create(SAMPLE_RATE, test_callback, &cb_state);

        /* Feed speech in chunks smaller than frame size */
        int16_t speech[80]; /* Half a frame */
        generate_speech_frame(speech, 80);

        /* Feed 40 half-frames = 20 full frames = 200ms (meets minimum) */
        for (int i = 0; i < 40; ++i) {
            sentence_segmenter_feed_audio(seg, speech, 80, 0);
        }
        RC_ASSERT(sentence_segmenter_get_state(seg) == SEG_STATE_ACCUMULATING);

        /* Now feed silence to trigger lock */
        int16_t silence[80];
        generate_silence_frame(silence, 80);
        /* Need 400ms of silence = 40 frames = 80 half-frames */
        for (int i = 0; i < 80; ++i) {
            sentence_segmenter_feed_audio(seg, silence, 80, 0);
        }

        RC_ASSERT(cb_state.call_count == 1);
        RC_ASSERT(sentence_segmenter_get_state(seg) == SEG_STATE_IDLE);

        sentence_segmenter_destroy(seg);
    });

    /* Test 16: Punctuation in Idle state has no effect */
    rc::check("Punctuation in Idle has no effect", []() {
        CallbackState cb_state;
        auto* seg = sentence_segmenter_create(SAMPLE_RATE, test_callback, &cb_state);

        sentence_segmenter_notify_punctuation(seg);
        RC_ASSERT(sentence_segmenter_get_state(seg) == SEG_STATE_IDLE);
        RC_ASSERT(cb_state.call_count == 0);

        sentence_segmenter_destroy(seg);
    });

    return 0;
}
