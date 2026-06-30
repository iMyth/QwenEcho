/**
 * @file audio_collector.h
 * @brief Audio Collector thread — captures PCM audio and writes to Ring Buffer.
 *
 * The Audio Collector is a dedicated real-time priority thread that:
 * 1. Configures platform audio input (16kHz, 16-bit, mono) via HAL
 * 2. Runs at highest real-time priority (SCHED_FIFO / RT QoS)
 * 3. Writes captured audio continuously to the Ring Buffer (lock-free)
 * 4. Detects sample drops exceeding 160 samples (10ms) and reports MSG_SAMPLE_DROP
 * 5. Produces first samples in the Ring Buffer within 50ms of pipeline start
 *
 * The AudioCollector does NOT own the Ring Buffer — it is passed in from the
 * Engine Manager which owns the buffer's lifetime.
 *
 * Validates: Requirements 3.1, 3.2, 3.3, 3.6, 3.7
 */

#ifndef AUDIO_COLLECTOR_H
#define AUDIO_COLLECTOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration — actual type is AudioRingBuffer (C++ class).
 * Use 'class' keyword here to match the definition in audio_ring_buffer.h. */
#ifdef __cplusplus
class AudioRingBuffer;
#else
typedef struct AudioRingBuffer AudioRingBuffer;
#endif

/**
 * Opaque Audio Collector handle.
 */
typedef struct AudioCollector AudioCollector;

/**
 * Create an Audio Collector instance.
 *
 * @param ring_buffer  Pointer to the shared AudioRingBuffer (owned by Engine Manager).
 *                     Must remain valid for the lifetime of the AudioCollector.
 * @return Pointer to new AudioCollector, or NULL on failure (e.g. NULL ring_buffer).
 */
AudioCollector* audio_collector_create(AudioRingBuffer* ring_buffer);

/**
 * Start audio capture.
 *
 * Sets the current thread to real-time priority, creates the platform audio
 * capture instance (16kHz, mono), and begins delivering samples to the Ring Buffer.
 *
 * First samples must appear in the Ring Buffer within 50ms of this call.
 *
 * @param collector  Audio Collector instance (must not be NULL).
 * @return 0 on success, negative error code on failure.
 */
int audio_collector_start(AudioCollector* collector);

/**
 * Stop audio capture.
 *
 * Stops the platform audio capture and ceases writing to the Ring Buffer.
 * No further callbacks will be delivered after this function returns.
 *
 * @param collector  Audio Collector instance (must not be NULL).
 */
void audio_collector_stop(AudioCollector* collector);

/**
 * Destroy the Audio Collector and release all resources.
 *
 * If capture is still running, it will be stopped first.
 *
 * @param collector  Audio Collector instance to destroy. NULL is safely ignored.
 */
void audio_collector_destroy(AudioCollector* collector);

/**
 * Query whether the Audio Collector is currently capturing audio.
 *
 * @param collector  Audio Collector instance (must not be NULL).
 * @return true if capture is active, false otherwise.
 */
bool audio_collector_is_running(const AudioCollector* collector);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_COLLECTOR_H */
