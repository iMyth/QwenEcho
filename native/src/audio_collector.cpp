/**
 * QwenEcho Audio Collector — Implementation.
 *
 * Captures PCM audio at 16kHz/16-bit/mono via the platform HAL, writes
 * samples into a shared lock-free Ring Buffer, and monitors for sample drops.
 *
 * Threading model:
 * - audio_collector_start() sets the calling context to real-time priority
 * - The HAL audio callback runs on a platform real-time audio thread
 * - The callback writes directly to the Ring Buffer (lock-free, RT-safe)
 * - Sample drop detection uses expected vs actual sample counting
 *
 * Validates: Requirements 3.1, 3.2, 3.3, 3.6, 3.7
 */

#include "audio_collector.h"
#include "audio_ring_buffer.h"
#include "native_port.h"
#include "hal_audio.h"
#include "hal_thread.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <chrono>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/** Audio capture sample rate in Hz. */
static constexpr uint32_t kSampleRate = 16000;

/** Number of audio channels (mono). */
static constexpr uint32_t kChannels = 1;

/**
 * Sample drop threshold: 160 samples = 10ms at 16kHz.
 * Drops exceeding this threshold trigger MSG_SAMPLE_DROP.
 */
static constexpr uint32_t kDropThreshold = 160;

// ---------------------------------------------------------------------------
// AudioCollector internal structure
// ---------------------------------------------------------------------------

struct AudioCollector {
    /** Non-owning pointer to the shared ring buffer. */
    AudioRingBuffer* ring_buffer;

    /** Platform audio capture handle (owned). */
    AudioCapture* capture;

    /** Running state flag — atomic for thread-safe queries. */
    std::atomic<bool> running;

    /**
     * Expected cumulative sample count since capture start.
     * Updated in the callback based on elapsed time.
     * Used to detect gaps (drops) in the audio stream.
     */
    std::atomic<uint64_t> expected_samples;

    /**
     * Actual cumulative sample count received via callbacks.
     */
    std::atomic<uint64_t> actual_samples;

    /**
     * Timestamp (steady clock) when capture started.
     * Used to compute expected sample count based on elapsed time.
     */
    std::chrono::steady_clock::time_point start_time;
};

// ---------------------------------------------------------------------------
// Audio capture callback (runs on RT audio thread)
// ---------------------------------------------------------------------------

/**
 * HAL audio callback — invoked from the platform's real-time audio thread.
 *
 * This function MUST NOT:
 * - Allocate or free memory
 * - Block (no mutexes, no I/O)
 * - Call non-RT-safe functions
 *
 * It MAY:
 * - Write to the lock-free ring buffer
 * - Update atomic counters
 * - Call native_port_post_sample_drop (which is lock-free)
 */
static void audio_capture_callback(const int16_t* samples, uint32_t count, void* user)
{
    AudioCollector* collector = static_cast<AudioCollector*>(user);

    if (!collector || !collector->running.load(std::memory_order_acquire)) {
        return;
    }

    // Write samples to the ring buffer (lock-free, never blocks)
    collector->ring_buffer->write(samples, count);

    // Update actual sample count
    uint64_t prev_actual = collector->actual_samples.load(std::memory_order_relaxed);
    uint64_t new_actual = prev_actual + count;
    collector->actual_samples.store(new_actual, std::memory_order_relaxed);

    // Compute expected samples based on elapsed time
    auto now = std::chrono::steady_clock::now();
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
        now - collector->start_time).count();
    uint64_t expected = static_cast<uint64_t>(elapsed_us) * kSampleRate / 1000000ULL;
    collector->expected_samples.store(expected, std::memory_order_relaxed);

    // Detect sample drops: if expected significantly exceeds actual, we have a gap
    if (expected > new_actual) {
        uint64_t gap = expected - new_actual;
        if (gap > kDropThreshold) {
            // Report sample drop via native port
            uint64_t timestamp_ms = static_cast<uint64_t>(elapsed_us / 1000);
            native_port_post_sample_drop(static_cast<int32_t>(gap), timestamp_ms);

            // Reset actual to expected to avoid repeated reports for the same gap
            collector->actual_samples.store(expected, std::memory_order_relaxed);
        }
    }
}

// ---------------------------------------------------------------------------
// Public API implementation
// ---------------------------------------------------------------------------

extern "C" {

AudioCollector* audio_collector_create(AudioRingBuffer* ring_buffer)
{
    if (!ring_buffer) {
        return nullptr;
    }

    AudioCollector* collector = static_cast<AudioCollector*>(
        std::calloc(1, sizeof(AudioCollector)));
    if (!collector) {
        return nullptr;
    }

    collector->ring_buffer = ring_buffer;
    collector->capture = nullptr;
    collector->running.store(false, std::memory_order_relaxed);
    collector->expected_samples.store(0, std::memory_order_relaxed);
    collector->actual_samples.store(0, std::memory_order_relaxed);

    return collector;
}

int audio_collector_start(AudioCollector* collector)
{
    if (!collector) {
        return -1;
    }

    if (collector->running.load(std::memory_order_acquire)) {
        return -2; // Already running
    }

    // Set real-time thread priority (SCHED_FIFO on Android, RT QoS on iOS)
    // This elevates the calling thread; the HAL audio callback thread
    // typically already runs at RT priority from the platform.
    hal_thread_set_realtime_priority();

    // Create the platform audio capture instance (16kHz, mono)
    collector->capture = hal_audio_capture_create(kSampleRate, kChannels);
    if (!collector->capture) {
        return -3; // Failed to create audio capture
    }

    // Initialize sample tracking counters
    collector->expected_samples.store(0, std::memory_order_relaxed);
    collector->actual_samples.store(0, std::memory_order_relaxed);

    // Record start time for expected sample computation
    collector->start_time = std::chrono::steady_clock::now();

    // Mark as running BEFORE starting capture so the callback sees it
    collector->running.store(true, std::memory_order_release);

    // Start audio capture with our callback
    int result = hal_audio_capture_start(collector->capture,
                                         audio_capture_callback,
                                         collector);
    if (result != 0) {
        // Failed to start — rollback
        collector->running.store(false, std::memory_order_release);
        hal_audio_capture_destroy(collector->capture);
        collector->capture = nullptr;
        return -4;
    }

    return 0; // Success
}

void audio_collector_stop(AudioCollector* collector)
{
    if (!collector) {
        return;
    }

    if (!collector->running.load(std::memory_order_acquire)) {
        return; // Not running
    }

    // Signal stop — callback will see this and exit early
    collector->running.store(false, std::memory_order_release);

    // Stop the platform audio capture (no further callbacks after return)
    if (collector->capture) {
        hal_audio_capture_stop(collector->capture);
        hal_audio_capture_destroy(collector->capture);
        collector->capture = nullptr;
    }
}

void audio_collector_destroy(AudioCollector* collector)
{
    if (!collector) {
        return;
    }

    // Stop if still running
    audio_collector_stop(collector);

    std::free(collector);
}

bool audio_collector_is_running(const AudioCollector* collector)
{
    if (!collector) {
        return false;
    }
    return collector->running.load(std::memory_order_acquire);
}

} // extern "C"
