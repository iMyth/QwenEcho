/**
 * @file hal_audio.h
 * @brief Audio capture HAL interface.
 *
 * Abstracts platform audio input for microphone capture:
 *   - Android: AAudio (low-latency mode)
 *   - iOS: AVAudioEngine (input tap)
 */

#ifndef HAL_AUDIO_H
#define HAL_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Audio capture callback function type.
 *
 * Called from the audio capture thread when new samples are available.
 *
 * @param samples  Pointer to PCM 16-bit samples.
 * @param count    Number of samples in the buffer.
 * @param user     User-provided context pointer.
 */
typedef void (*hal_audio_capture_callback_t)(const int16_t* samples,
                                             uint32_t count, void* user);

/**
 * Opaque audio capture handle.
 */
typedef struct AudioCapture AudioCapture;

/**
 * Create an audio capture instance.
 *
 * @param sample_rate  Desired sample rate in Hz (e.g. 16000).
 * @param channels     Number of audio channels (1 for mono).
 * @return Pointer to new AudioCapture, or NULL on failure.
 */
AudioCapture* hal_audio_capture_create(uint32_t sample_rate, uint32_t channels);

/**
 * Start audio capture and begin delivering samples via callback.
 *
 * The callback is invoked from a real-time audio thread. Avoid blocking
 * or allocating memory within the callback.
 *
 * @param cap   Audio capture instance (must not be NULL).
 * @param cb    Callback function to receive audio samples.
 * @param user  User context pointer passed to the callback.
 * @return 0 on success, negative error code on failure.
 */
int hal_audio_capture_start(AudioCapture* cap, hal_audio_capture_callback_t cb,
                            void* user);

/**
 * Stop audio capture. No further callbacks will be delivered after return.
 *
 * @param cap  Audio capture instance (must not be NULL).
 */
void hal_audio_capture_stop(AudioCapture* cap);

/**
 * Destroy the audio capture instance and release all resources.
 *
 * @param cap  Audio capture instance to destroy. NULL is safely ignored.
 */
void hal_audio_capture_destroy(AudioCapture* cap);

#ifdef __cplusplus
}
#endif

#endif /* HAL_AUDIO_H */
