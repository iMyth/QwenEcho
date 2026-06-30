/**
 * Unit tests for the ASR Processing Stage.
 *
 * Tests the core ASR stage behaviors:
 * - Segment processing and confirmed text generation
 * - Noise/silence discard (no MSG_ASR_CONFIRMED for noise)
 * - Throttle mode downsampling (16kHz → 8kHz)
 * - Output queue population with confirmed results
 * - Worker thread lifecycle (create/destroy)
 *
 * Validates: Requirements 5.1, 5.2, 5.3, 5.4, 5.5, 5.6, 10.5
 */

#include <rapidcheck.h>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <cassert>
#include <cstdio>

#include "asr_stage.h"
#include "bounded_spsc_queue.h"
#include "echo_types.h"
#include "sentence_segmenter.h"
#include "native_port.h"

/* --------------------------------------------------------------------------
 * Test helpers
 * -------------------------------------------------------------------------- */

/**
 * Generate speech-like audio (energy well above noise threshold of 150).
 */
static std::vector<int16_t> generate_speech_audio(uint32_t sample_count) {
    std::vector<int16_t> audio(sample_count);
    for (uint32_t i = 0; i < sample_count; ++i) {
        audio[i] = static_cast<int16_t>(500 + (i % 300));
    }
    return audio;
}

/**
 * Generate noise audio (energy below threshold of 150).
 */
static std::vector<int16_t> generate_noise_audio(uint32_t sample_count) {
    std::vector<int16_t> audio(sample_count);
    for (uint32_t i = 0; i < sample_count; ++i) {
        audio[i] = static_cast<int16_t>((i % 5) - 2); /* Near-zero */
    }
    return audio;
}

/**
 * Wait for the ASR worker to process pending segments.
 */
static void wait_for_processing(uint32_t max_wait_ms = 100) {
    std::this_thread::sleep_for(std::chrono::milliseconds(max_wait_ms));
}

/* --------------------------------------------------------------------------
 * Simple assertion helper for non-property tests
 * -------------------------------------------------------------------------- */

static int g_test_count = 0;
static int g_pass_count = 0;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAILED: %s (line %d)\n", #cond, __LINE__); \
        return false; \
    } \
} while(0)

static bool run_test(const char* name, bool (*fn)()) {
    g_test_count++;
    std::printf("- %s\n", name);
    if (fn()) {
        g_pass_count++;
        std::printf("  OK\n");
        return true;
    }
    return false;
}

/* --------------------------------------------------------------------------
 * Unit Tests
 * -------------------------------------------------------------------------- */

static bool test_create_destroy() {
    BoundedSPSCQueue<AsrToLlmElement> queue;
    AsrStage* stage = asr_stage_create(nullptr, &queue);
    TEST_ASSERT(stage != nullptr);
    asr_stage_destroy(stage);
    return true;
}

static bool test_create_null_queue() {
    AsrStage* stage = asr_stage_create(nullptr, nullptr);
    TEST_ASSERT(stage == nullptr);
    return true;
}

static bool test_speech_produces_output() {
    BoundedSPSCQueue<AsrToLlmElement> queue;
    AsrStage* stage = asr_stage_create(nullptr, &queue);
    TEST_ASSERT(stage != nullptr);

    /* Create a speech segment (500ms = 8000 samples at 16kHz) */
    auto audio = generate_speech_audio(8000);
    LockedSegment segment;
    segment.audio_data = audio.data();
    segment.sample_count = static_cast<uint32_t>(audio.size());
    segment.segment_id = 42;
    segment.speaker_id = 0;
    segment.timestamp_ms = 1000;

    int result = asr_stage_process_segment(stage, &segment);
    TEST_ASSERT(result == 0);

    wait_for_processing(150);

    /* Check output queue has an element */
    AsrToLlmElement element;
    bool popped = queue.try_pop(element);
    TEST_ASSERT(popped);
    TEST_ASSERT(element.segment_id == 42);
    TEST_ASSERT(element.speaker_id == 0);
    TEST_ASSERT(element.timestamp_ms == 1000);
    TEST_ASSERT(element.text_len > 0);
    TEST_ASSERT(std::strlen(element.text) > 0);

    asr_stage_destroy(stage);
    return true;
}

static bool test_noise_discarded() {
    BoundedSPSCQueue<AsrToLlmElement> queue;
    AsrStage* stage = asr_stage_create(nullptr, &queue);
    TEST_ASSERT(stage != nullptr);

    /* Create a noise-only segment */
    auto audio = generate_noise_audio(8000);
    LockedSegment segment;
    segment.audio_data = audio.data();
    segment.sample_count = static_cast<uint32_t>(audio.size());
    segment.segment_id = 1;
    segment.speaker_id = 0;
    segment.timestamp_ms = 500;

    int result = asr_stage_process_segment(stage, &segment);
    TEST_ASSERT(result == 0);

    wait_for_processing(150);

    /* Queue should be empty — noise was discarded */
    AsrToLlmElement element;
    bool popped = queue.try_pop(element);
    TEST_ASSERT(!popped);

    asr_stage_destroy(stage);
    return true;
}

static bool test_null_segment_error() {
    BoundedSPSCQueue<AsrToLlmElement> queue;
    AsrStage* stage = asr_stage_create(nullptr, &queue);
    TEST_ASSERT(stage != nullptr);

    int result = asr_stage_process_segment(stage, nullptr);
    TEST_ASSERT(result < 0);

    asr_stage_destroy(stage);
    return true;
}

static bool test_null_stage_error() {
    LockedSegment segment;
    int16_t audio[100] = {0};
    segment.audio_data = audio;
    segment.sample_count = 100;
    segment.segment_id = 0;
    segment.speaker_id = 0;
    segment.timestamp_ms = 0;

    int result = asr_stage_process_segment(nullptr, &segment);
    TEST_ASSERT(result < 0);
    return true;
}

static bool test_thermal_mode_set() {
    BoundedSPSCQueue<AsrToLlmElement> queue;
    AsrStage* stage = asr_stage_create(nullptr, &queue);
    TEST_ASSERT(stage != nullptr);

    /* Should not crash */
    asr_stage_set_thermal_mode(stage, 1);
    asr_stage_set_thermal_mode(stage, 0);
    asr_stage_set_thermal_mode(stage, 1);

    asr_stage_destroy(stage);
    return true;
}

static bool test_throttle_mode_output() {
    BoundedSPSCQueue<AsrToLlmElement> queue;
    AsrStage* stage = asr_stage_create(nullptr, &queue);
    TEST_ASSERT(stage != nullptr);

    /* Enable throttle mode */
    asr_stage_set_thermal_mode(stage, 1);

    /* Create a speech segment */
    auto audio = generate_speech_audio(8000);
    LockedSegment segment;
    segment.audio_data = audio.data();
    segment.sample_count = static_cast<uint32_t>(audio.size());
    segment.segment_id = 7;
    segment.speaker_id = 1;
    segment.timestamp_ms = 2000;

    int result = asr_stage_process_segment(stage, &segment);
    TEST_ASSERT(result == 0);

    wait_for_processing(150);

    /* Should still produce output */
    AsrToLlmElement element;
    bool popped = queue.try_pop(element);
    TEST_ASSERT(popped);
    TEST_ASSERT(element.segment_id == 7);
    TEST_ASSERT(element.speaker_id == 1);
    TEST_ASSERT(element.text_len > 0);

    asr_stage_destroy(stage);
    return true;
}

static bool test_multiple_segments_in_order() {
    BoundedSPSCQueue<AsrToLlmElement> queue;
    AsrStage* stage = asr_stage_create(nullptr, &queue);
    TEST_ASSERT(stage != nullptr);

    /* Submit 3 segments */
    for (uint32_t i = 0; i < 3; ++i) {
        auto audio = generate_speech_audio(4800); /* 300ms */
        LockedSegment segment;
        segment.audio_data = audio.data();
        segment.sample_count = static_cast<uint32_t>(audio.size());
        segment.segment_id = i + 10;
        segment.speaker_id = 0;
        segment.timestamp_ms = (i + 1) * 1000;

        asr_stage_process_segment(stage, &segment);
    }

    wait_for_processing(200);

    /* All 3 should be in the queue in order */
    for (uint32_t i = 0; i < 3; ++i) {
        AsrToLlmElement element;
        bool popped = queue.try_pop(element);
        TEST_ASSERT(popped);
        TEST_ASSERT(element.segment_id == i + 10);
    }

    asr_stage_destroy(stage);
    return true;
}

static bool test_empty_audio_error() {
    BoundedSPSCQueue<AsrToLlmElement> queue;
    AsrStage* stage = asr_stage_create(nullptr, &queue);
    TEST_ASSERT(stage != nullptr);

    LockedSegment segment;
    segment.audio_data = nullptr;
    segment.sample_count = 0;
    segment.segment_id = 0;
    segment.speaker_id = 0;
    segment.timestamp_ms = 0;

    int result = asr_stage_process_segment(stage, &segment);
    TEST_ASSERT(result < 0);

    asr_stage_destroy(stage);
    return true;
}

static bool test_destroy_with_pending() {
    BoundedSPSCQueue<AsrToLlmElement> queue;
    AsrStage* stage = asr_stage_create(nullptr, &queue);
    TEST_ASSERT(stage != nullptr);

    /* Submit several segments */
    for (int i = 0; i < 5; ++i) {
        auto audio = generate_speech_audio(16000); /* 1 second */
        LockedSegment segment;
        segment.audio_data = audio.data();
        segment.sample_count = static_cast<uint32_t>(audio.size());
        segment.segment_id = static_cast<uint32_t>(i);
        segment.speaker_id = 0;
        segment.timestamp_ms = static_cast<uint64_t>(i) * 1000;
        asr_stage_process_segment(stage, &segment);
    }

    /* Destroy immediately without waiting — should not crash */
    asr_stage_destroy(stage);
    return true;
}

static bool test_null_destroy_safe() {
    asr_stage_destroy(nullptr);
    return true;
}

static bool test_null_thermal_safe() {
    asr_stage_set_thermal_mode(nullptr, 1);
    return true;
}

static bool test_confirmed_text_format() {
    BoundedSPSCQueue<AsrToLlmElement> queue;
    AsrStage* stage = asr_stage_create(nullptr, &queue);
    TEST_ASSERT(stage != nullptr);

    auto audio = generate_speech_audio(8000);
    LockedSegment segment;
    segment.audio_data = audio.data();
    segment.sample_count = static_cast<uint32_t>(audio.size());
    segment.segment_id = 99;
    segment.speaker_id = 0;
    segment.timestamp_ms = 5000;

    asr_stage_process_segment(stage, &segment);
    wait_for_processing(150);

    AsrToLlmElement element;
    bool popped = queue.try_pop(element);
    TEST_ASSERT(popped);

    /* Text should be null-terminated */
    TEST_ASSERT(element.text[element.text_len] == '\0');
    /* text_len should match strlen */
    TEST_ASSERT(element.text_len == static_cast<uint16_t>(std::strlen(element.text)));
    /* text_len should be within buffer bounds */
    TEST_ASSERT(element.text_len < sizeof(element.text));

    asr_stage_destroy(stage);
    return true;
}

/* --------------------------------------------------------------------------
 * Property-based test: varying segment sizes always produce valid output
 * -------------------------------------------------------------------------- */

int main() {
    /* Deterministic unit tests (involve sleep, run once each) */
    run_test("Create and destroy lifecycle", test_create_destroy);
    run_test("Create with null queue returns nullptr", test_create_null_queue);
    run_test("Speech segment produces confirmed text in output queue", test_speech_produces_output);
    run_test("Noise segment is silently discarded", test_noise_discarded);
    run_test("Null segment returns error", test_null_segment_error);
    run_test("Null stage returns error", test_null_stage_error);
    run_test("Thermal mode can be set", test_thermal_mode_set);
    run_test("Throttle mode produces output from downsampled audio", test_throttle_mode_output);
    run_test("Multiple segments processed in order", test_multiple_segments_in_order);
    run_test("Empty audio data returns error", test_empty_audio_error);
    run_test("Destroy with pending segments is safe", test_destroy_with_pending);
    run_test("Null destroy is safe", test_null_destroy_safe);
    run_test("Null thermal mode set is safe", test_null_thermal_safe);
    run_test("Confirmed text is null-terminated within bounds", test_confirmed_text_format);

    /* Property-based test: random speech durations always produce valid output */
    rc::check("Random speech duration produces valid output in queue", []() {
        /* Generate a random segment duration between 200ms and 2000ms */
        auto duration_ms = *rc::gen::inRange(200, 2001);
        uint32_t sample_count = static_cast<uint32_t>((duration_ms * 16000) / 1000);

        BoundedSPSCQueue<AsrToLlmElement> queue;
        AsrStage* stage = asr_stage_create(nullptr, &queue);
        RC_ASSERT(stage != nullptr);

        auto audio = generate_speech_audio(sample_count);
        LockedSegment segment;
        segment.audio_data = audio.data();
        segment.sample_count = sample_count;
        segment.segment_id = static_cast<uint32_t>(duration_ms);
        segment.speaker_id = static_cast<uint8_t>(duration_ms % 2);
        segment.timestamp_ms = static_cast<uint64_t>(duration_ms) * 10;

        int result = asr_stage_process_segment(stage, &segment);
        RC_ASSERT(result == 0);

        /* Wait for processing — use short wait since stub is fast */
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        AsrToLlmElement element;
        bool popped = queue.try_pop(element);
        RC_ASSERT(popped);
        RC_ASSERT(element.segment_id == static_cast<uint32_t>(duration_ms));
        RC_ASSERT(element.speaker_id == static_cast<uint8_t>(duration_ms % 2));
        RC_ASSERT(element.text_len > 0);
        RC_ASSERT(element.text[element.text_len] == '\0');
        RC_ASSERT(element.text_len < sizeof(element.text));

        asr_stage_destroy(stage);
    });

    std::printf("\n%d/%d tests passed\n", g_pass_count, g_test_count);

    if (g_pass_count != g_test_count) {
        return 1;
    }
    return 0;
}
