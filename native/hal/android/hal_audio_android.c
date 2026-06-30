/**
 * @file hal_audio_android.c
 * @brief Android HAL audio backend using AAudio low-latency API.
 *
 * Implements the hal_audio.h interface for Android using AAudio
 * with AAUDIO_PERFORMANCE_MODE_LOW_LATENCY for minimal input latency.
 *
 * Requires Android NDK r21+ and targets Android 8.1+ (API level 27+).
 */

#ifdef __ANDROID__

#include "hal_audio.h"
#include <stdlib.h>
#include <string.h>
#include <android/log.h>
#include <aaudio/AAudio.h>

#define LOG_TAG "QwenEcho_Audio"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* ─── Audio Capture Context ─────────────────────────────────────────────────── */

struct AudioCapture {
    AAudioStream* stream;
    uint32_t sample_rate;
    uint32_t channels;
    hal_audio_capture_callback_t callback;
    void* user_data;
    int running;
};

/* ─── AAudio Data Callback ──────────────────────────────────────────────────── */

/**
 * AAudio data callback invoked from the audio thread when input data is ready.
 *
 * This callback runs on a real-time priority thread managed by the AAudio
 * framework. We forward the PCM samples directly to the user-provided callback.
 *
 * IMPORTANT: No blocking, allocation, or I/O operations in this function.
 */
static aaudio_data_callback_result_t audio_data_callback(
        AAudioStream* stream,
        void* userData,
        void* audioData,
        int32_t numFrames) {
    (void)stream;
    AudioCapture* cap = (AudioCapture*)userData;

    if (cap && cap->callback && cap->running) {
        /*
         * AAudio delivers data as int16_t for AAUDIO_FORMAT_PCM_I16.
         * numFrames is the number of frames; for mono, frames == samples.
         */
        const int16_t* samples = (const int16_t*)audioData;
        uint32_t count = (uint32_t)numFrames * cap->channels;
        cap->callback(samples, count, cap->user_data);
    }

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

/**
 * AAudio error callback invoked when the stream encounters an error.
 * Typically this means the stream has been disconnected (e.g., USB audio removed).
 */
static void audio_error_callback(
        AAudioStream* stream,
        void* userData,
        aaudio_result_t error) {
    (void)stream;
    (void)userData;
    LOGE("AAudio stream error: %s (code %d)",
         AAudio_convertResultToText(error), error);
    /*
     * In production, we would notify the engine to restart the stream
     * or switch to a different audio device.
     */
}

/* ─── Public HAL Interface Implementation ───────────────────────────────────── */

AudioCapture* hal_audio_capture_create(uint32_t sample_rate, uint32_t channels) {
    AudioCapture* cap = (AudioCapture*)calloc(1, sizeof(AudioCapture));
    if (!cap) {
        LOGE("Failed to allocate AudioCapture");
        return NULL;
    }

    cap->sample_rate = sample_rate;
    cap->channels = channels;
    cap->running = 0;

    LOGI("AudioCapture created: %u Hz, %u ch", sample_rate, channels);
    return cap;
}

int hal_audio_capture_start(AudioCapture* cap, hal_audio_capture_callback_t cb,
                            void* user) {
    if (!cap) return -1;
    if (!cb) return -2;
    if (cap->running) return -3;

    cap->callback = cb;
    cap->user_data = user;

    /* Build AAudio stream with low-latency configuration */
    AAudioStreamBuilder* builder = NULL;
    aaudio_result_t result = AAudio_createStreamBuilder(&builder);
    if (result != AAUDIO_OK) {
        LOGE("AAudio_createStreamBuilder failed: %s", AAudio_convertResultToText(result));
        return -4;
    }

    /* Configure for low-latency audio input */
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
    AAudioStreamBuilder_setSampleRate(builder, (int32_t)cap->sample_rate);
    AAudioStreamBuilder_setChannelCount(builder, (int32_t)cap->channels);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);

    /*
     * Use callback mode for lowest latency. The AAudio framework manages
     * the real-time callback thread internally.
     */
    AAudioStreamBuilder_setDataCallback(builder, audio_data_callback, cap);
    AAudioStreamBuilder_setErrorCallback(builder, audio_error_callback, cap);

    /*
     * Request a small buffer for low latency.
     * The actual size will be determined by the hardware capabilities.
     */
    AAudioStreamBuilder_setFramesPerDataCallback(builder, 0); /* Let system choose */

    /* Open the stream */
    result = AAudioStreamBuilder_openStream(builder, &cap->stream);
    AAudioStreamBuilder_delete(builder);

    if (result != AAUDIO_OK) {
        LOGE("AAudioStreamBuilder_openStream failed: %s", AAudio_convertResultToText(result));
        cap->stream = NULL;
        return -5;
    }

    /* Log actual stream parameters */
    int32_t actual_rate = AAudioStream_getSampleRate(cap->stream);
    int32_t actual_channels = AAudioStream_getChannelCount(cap->stream);
    int32_t buffer_size = AAudioStream_getBufferSizeInFrames(cap->stream);
    int32_t burst_size = AAudioStream_getFramesPerBurst(cap->stream);
    LOGI("AAudio stream opened: rate=%d, ch=%d, buffer=%d, burst=%d",
         actual_rate, actual_channels, buffer_size, burst_size);

    /*
     * Set buffer size to 2x burst size for a good balance between
     * latency and glitch prevention.
     */
    AAudioStream_setBufferSizeInFrames(cap->stream, burst_size * 2);

    /* Start capture */
    result = AAudioStream_requestStart(cap->stream);
    if (result != AAUDIO_OK) {
        LOGE("AAudioStream_requestStart failed: %s", AAudio_convertResultToText(result));
        AAudioStream_close(cap->stream);
        cap->stream = NULL;
        return -6;
    }

    cap->running = 1;
    LOGI("AAudio capture started");
    return 0;
}

void hal_audio_capture_stop(AudioCapture* cap) {
    if (!cap || !cap->running) return;

    cap->running = 0;

    if (cap->stream) {
        aaudio_result_t result = AAudioStream_requestStop(cap->stream);
        if (result != AAUDIO_OK) {
            LOGW("AAudioStream_requestStop: %s", AAudio_convertResultToText(result));
        }

        /* Wait for the stream to fully stop before closing */
        aaudio_stream_state_t next_state = AAUDIO_STREAM_STATE_UNINITIALIZED;
        AAudioStream_waitForStateChange(cap->stream, AAUDIO_STREAM_STATE_STOPPING,
                                        &next_state, 1000000000LL /* 1 second */);
    }

    LOGI("AAudio capture stopped");
}

void hal_audio_capture_destroy(AudioCapture* cap) {
    if (!cap) return;

    if (cap->running) {
        hal_audio_capture_stop(cap);
    }

    if (cap->stream) {
        AAudioStream_close(cap->stream);
        cap->stream = NULL;
    }

    LOGI("AudioCapture destroyed");
    free(cap);
}

#endif /* __ANDROID__ */
