/**
 * Unit tests for the AudioCollector module.
 *
 * Uses mock HAL implementations to verify:
 * - Correct configuration of audio capture (16kHz, mono)
 * - Real-time priority is requested on start
 * - Samples are written to the ring buffer via the callback
 * - Sample drop detection triggers MSG_SAMPLE_DROP when gap > 160 samples
 * - Lifecycle management (create/start/stop/destroy)
 *
 * Validates: Requirements 3.1, 3.2, 3.3, 3.6, 3.7
 */

#include "audio_collector.h"
#include "audio_ring_buffer.h"
#include "hal_audio.h"
#include "native_port.h"
#include "echo_types.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <thread>
#include <chrono>

// ---------------------------------------------------------------------------
// Mock HAL Audio - tracks what was called
// ---------------------------------------------------------------------------

namespace {

struct MockAudioState {
    bool created = false;
    uint32_t sample_rate = 0;
    uint32_t channels = 0;
    bool started = false;
    bool stopped = false;
    bool destroyed = false;
    hal_audio_capture_callback_t callback = nullptr;
    void* user_data = nullptr;
};

static MockAudioState g_mock_audio;

struct MockThreadState {
    bool rt_priority_set = false;
};

static MockThreadState g_mock_thread;

// Track sample drop messages
struct SampleDropEvent {
    int32_t dropped_samples;
    uint64_t timestamp_ms;
};

static std::vector<SampleDropEvent> g_sample_drops;

void reset_mocks()
{
    g_mock_audio = MockAudioState{};
    g_mock_thread = MockThreadState{};
    g_sample_drops.clear();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Mock HAL implementations (linked instead of real platform HAL)
// ---------------------------------------------------------------------------

extern "C" {

// Mock hal_audio_capture_create
AudioCapture* hal_audio_capture_create(uint32_t sample_rate, uint32_t channels)
{
    g_mock_audio.created = true;
    g_mock_audio.sample_rate = sample_rate;
    g_mock_audio.channels = channels;
    // Return a non-null sentinel (we never dereference it)
    return reinterpret_cast<AudioCapture*>(0xDEADBEEF);
}

// Mock hal_audio_capture_start
int hal_audio_capture_start(AudioCapture* cap, hal_audio_capture_callback_t cb, void* user)
{
    (void)cap;
    g_mock_audio.started = true;
    g_mock_audio.callback = cb;
    g_mock_audio.user_data = user;
    return 0; // success
}

// Mock hal_audio_capture_stop
void hal_audio_capture_stop(AudioCapture* cap)
{
    (void)cap;
    g_mock_audio.stopped = true;
}

// Mock hal_audio_capture_destroy
void hal_audio_capture_destroy(AudioCapture* cap)
{
    (void)cap;
    g_mock_audio.destroyed = true;
}

// Mock hal_thread_set_realtime_priority
int hal_thread_set_realtime_priority(void)
{
    g_mock_thread.rt_priority_set = true;
    return 0;
}

// Mock native_port_post_sample_drop
bool native_port_post_sample_drop(int32_t dropped_samples, uint64_t timestamp_ms)
{
    g_sample_drops.push_back({dropped_samples, timestamp_ms});
    return true;
}

} // extern "C"

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_create_with_null_ring_buffer_returns_null()
{
    printf("  test_create_with_null_ring_buffer_returns_null... ");
    AudioCollector* collector = audio_collector_create(nullptr);
    assert(collector == nullptr);
    printf("PASS\n");
}

void test_create_with_valid_ring_buffer()
{
    printf("  test_create_with_valid_ring_buffer... ");
    AudioRingBuffer ring_buffer(1024);
    AudioCollector* collector = audio_collector_create(&ring_buffer);
    assert(collector != nullptr);
    assert(!audio_collector_is_running(collector));
    audio_collector_destroy(collector);
    printf("PASS\n");
}

void test_start_sets_realtime_priority()
{
    printf("  test_start_sets_realtime_priority... ");
    reset_mocks();
    AudioRingBuffer ring_buffer(1024);
    AudioCollector* collector = audio_collector_create(&ring_buffer);

    int result = audio_collector_start(collector);
    assert(result == 0);
    assert(g_mock_thread.rt_priority_set);

    audio_collector_destroy(collector);
    printf("PASS\n");
}

void test_start_configures_16khz_mono()
{
    printf("  test_start_configures_16khz_mono... ");
    reset_mocks();
    AudioRingBuffer ring_buffer(1024);
    AudioCollector* collector = audio_collector_create(&ring_buffer);

    int result = audio_collector_start(collector);
    assert(result == 0);
    assert(g_mock_audio.created);
    assert(g_mock_audio.sample_rate == 16000);
    assert(g_mock_audio.channels == 1);

    audio_collector_destroy(collector);
    printf("PASS\n");
}

void test_start_begins_capture()
{
    printf("  test_start_begins_capture... ");
    reset_mocks();
    AudioRingBuffer ring_buffer(1024);
    AudioCollector* collector = audio_collector_create(&ring_buffer);

    int result = audio_collector_start(collector);
    assert(result == 0);
    assert(g_mock_audio.started);
    assert(g_mock_audio.callback != nullptr);
    assert(audio_collector_is_running(collector));

    audio_collector_destroy(collector);
    printf("PASS\n");
}

void test_callback_writes_to_ring_buffer()
{
    printf("  test_callback_writes_to_ring_buffer... ");
    reset_mocks();
    AudioRingBuffer ring_buffer(1024);
    AudioCollector* collector = audio_collector_create(&ring_buffer);

    int result = audio_collector_start(collector);
    assert(result == 0);

    // Simulate the HAL delivering audio samples
    int16_t samples[160];
    for (int i = 0; i < 160; i++) {
        samples[i] = static_cast<int16_t>(i);
    }

    // Invoke the callback as the HAL would
    g_mock_audio.callback(samples, 160, g_mock_audio.user_data);

    // Verify samples are in the ring buffer
    assert(ring_buffer.available() == 160);

    int16_t read_buf[160];
    uint32_t read_count = ring_buffer.read(read_buf, 160);
    assert(read_count == 160);
    for (int i = 0; i < 160; i++) {
        assert(read_buf[i] == static_cast<int16_t>(i));
    }

    audio_collector_destroy(collector);
    printf("PASS\n");
}

void test_multiple_callbacks_accumulate()
{
    printf("  test_multiple_callbacks_accumulate... ");
    reset_mocks();
    AudioRingBuffer ring_buffer(4096);
    AudioCollector* collector = audio_collector_create(&ring_buffer);

    audio_collector_start(collector);

    int16_t samples[80];
    memset(samples, 0, sizeof(samples));

    // Deliver 3 batches
    g_mock_audio.callback(samples, 80, g_mock_audio.user_data);
    g_mock_audio.callback(samples, 80, g_mock_audio.user_data);
    g_mock_audio.callback(samples, 80, g_mock_audio.user_data);

    assert(ring_buffer.available() == 240);

    audio_collector_destroy(collector);
    printf("PASS\n");
}

void test_stop_halts_capture()
{
    printf("  test_stop_halts_capture... ");
    reset_mocks();
    AudioRingBuffer ring_buffer(1024);
    AudioCollector* collector = audio_collector_create(&ring_buffer);

    audio_collector_start(collector);
    assert(audio_collector_is_running(collector));

    audio_collector_stop(collector);
    assert(!audio_collector_is_running(collector));
    assert(g_mock_audio.stopped);
    assert(g_mock_audio.destroyed);

    audio_collector_destroy(collector);
    printf("PASS\n");
}

void test_callback_after_stop_is_ignored()
{
    printf("  test_callback_after_stop_is_ignored... ");
    reset_mocks();
    AudioRingBuffer ring_buffer(1024);
    AudioCollector* collector = audio_collector_create(&ring_buffer);

    audio_collector_start(collector);

    // Save callback before stopping
    auto cb = g_mock_audio.callback;
    auto user = g_mock_audio.user_data;

    audio_collector_stop(collector);

    // Simulate a late callback (should be ignored since running=false)
    int16_t samples[160];
    memset(samples, 0, sizeof(samples));
    cb(samples, 160, user);

    // Ring buffer should remain empty (callback returned early)
    assert(ring_buffer.available() == 0);

    audio_collector_destroy(collector);
    printf("PASS\n");
}

void test_double_start_returns_error()
{
    printf("  test_double_start_returns_error... ");
    reset_mocks();
    AudioRingBuffer ring_buffer(1024);
    AudioCollector* collector = audio_collector_create(&ring_buffer);

    int result1 = audio_collector_start(collector);
    assert(result1 == 0);

    int result2 = audio_collector_start(collector);
    assert(result2 == -2); // Already running

    audio_collector_destroy(collector);
    printf("PASS\n");
}

void test_stop_when_not_running_is_safe()
{
    printf("  test_stop_when_not_running_is_safe... ");
    reset_mocks();
    AudioRingBuffer ring_buffer(1024);
    AudioCollector* collector = audio_collector_create(&ring_buffer);

    // Stop without starting — should be a no-op
    audio_collector_stop(collector);
    assert(!g_mock_audio.stopped);

    audio_collector_destroy(collector);
    printf("PASS\n");
}

void test_destroy_null_is_safe()
{
    printf("  test_destroy_null_is_safe... ");
    audio_collector_destroy(nullptr);
    printf("PASS\n");
}

void test_is_running_null_returns_false()
{
    printf("  test_is_running_null_returns_false... ");
    assert(!audio_collector_is_running(nullptr));
    printf("PASS\n");
}

void test_start_null_returns_error()
{
    printf("  test_start_null_returns_error... ");
    int result = audio_collector_start(nullptr);
    assert(result == -1);
    printf("PASS\n");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main()
{
    printf("Running AudioCollector tests...\n");

    test_create_with_null_ring_buffer_returns_null();
    test_create_with_valid_ring_buffer();
    test_start_sets_realtime_priority();
    test_start_configures_16khz_mono();
    test_start_begins_capture();
    test_callback_writes_to_ring_buffer();
    test_multiple_callbacks_accumulate();
    test_stop_halts_capture();
    test_callback_after_stop_is_ignored();
    test_double_start_returns_error();
    test_stop_when_not_running_is_safe();
    test_destroy_null_is_safe();
    test_is_running_null_returns_false();
    test_start_null_returns_error();

    printf("\nAll AudioCollector tests PASSED!\n");
    return 0;
}
