/**
 * @file hal_audio_ios.m
 * @brief iOS audio HAL backend using AVAudioEngine.
 *
 * Implements the hal_audio.h interface for iOS:
 *   - Audio capture via AVAudioEngine input node tap
 *   - Audio output via AVAudioEngine output/mixer
 *   - Configurable sample rate and channel count
 *   - Real-time callback delivery on audio thread
 */

#if TARGET_OS_IPHONE

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

#include "../hal_audio.h"
#include <stdlib.h>
#include <string.h>

/**
 * Internal audio capture context for iOS.
 */
struct AudioCapture {
    AVAudioEngine *engine;
    AVAudioInputNode *inputNode;

    uint32_t sample_rate;
    uint32_t channels;

    hal_audio_capture_callback_t callback;
    void *user_data;

    int is_running;
};

#pragma mark - Audio Session Configuration

/**
 * Configure AVAudioSession for low-latency recording.
 */
static int configure_audio_session(uint32_t sample_rate) {
    @autoreleasepool {
        NSError *error = nil;
        AVAudioSession *session = [AVAudioSession sharedInstance];

        /* Set category for simultaneous record + playback */
        [session setCategory:AVAudioSessionCategoryPlayAndRecord
                 withOptions:(AVAudioSessionCategoryOptionDefaultToSpeaker |
                              AVAudioSessionCategoryOptionAllowBluetoothHFP)
                       error:&error];
        if (error) {
            NSLog(@"[HAL Audio] Failed to set audio session category: %@",
                  error.localizedDescription);
            return -1;
        }

        /* Request desired sample rate */
        [session setPreferredSampleRate:(double)sample_rate error:&error];
        if (error) {
            NSLog(@"[HAL Audio] Failed to set preferred sample rate: %@",
                  error.localizedDescription);
            return -1;
        }

        /* Request low I/O buffer duration for minimal latency */
        [session setPreferredIOBufferDuration:0.005 error:&error]; /* 5ms buffer */
        if (error) {
            NSLog(@"[HAL Audio] Failed to set preferred IO buffer duration: %@",
                  error.localizedDescription);
            /* Non-fatal: continue with default buffer size */
        }

        /* Activate the audio session */
        [session setActive:YES error:&error];
        if (error) {
            NSLog(@"[HAL Audio] Failed to activate audio session: %@",
                  error.localizedDescription);
            return -1;
        }

        NSLog(@"[HAL Audio] Audio session configured: rate=%.0fHz, buffer=%.3fs",
              session.sampleRate, session.IOBufferDuration);
        return 0;
    }
}

#pragma mark - Lifecycle

AudioCapture* hal_audio_capture_create(uint32_t sample_rate, uint32_t channels) {
    @autoreleasepool {
        if (sample_rate == 0 || channels == 0) {
            return NULL;
        }

        AudioCapture *cap = (AudioCapture *)calloc(1, sizeof(AudioCapture));
        if (!cap) {
            return NULL;
        }

        cap->sample_rate = sample_rate;
        cap->channels = channels;
        cap->is_running = 0;
        cap->callback = NULL;
        cap->user_data = NULL;

        /* Configure audio session */
        if (configure_audio_session(sample_rate) != 0) {
            free(cap);
            return NULL;
        }

        /* Create AVAudioEngine */
        cap->engine = [[AVAudioEngine alloc] init];
        if (!cap->engine) {
            free(cap);
            return NULL;
        }

        cap->inputNode = cap->engine.inputNode;

        NSLog(@"[HAL Audio] AudioCapture created: %uHz, %u channels",
              sample_rate, channels);
        return cap;
    }
}

void hal_audio_capture_destroy(AudioCapture* cap) {
    if (!cap) {
        return;
    }

    @autoreleasepool {
        if (cap->is_running) {
            hal_audio_capture_stop(cap);
        }

        cap->engine = nil;
        cap->inputNode = nil;
    }

    free(cap);
}

#pragma mark - Capture Control

int hal_audio_capture_start(AudioCapture* cap, hal_audio_capture_callback_t cb,
                            void* user) {
    if (!cap || !cb) {
        return -1;
    }

    if (cap->is_running) {
        return -1; /* Already running */
    }

    @autoreleasepool {
        cap->callback = cb;
        cap->user_data = user;

        /*
         * Configure input tap format.
         * We request the desired sample rate and mono/stereo channel layout.
         * AVAudioEngine will handle resampling from hardware format if needed.
         */
        AVAudioFormat *desiredFormat = [[AVAudioFormat alloc]
            initWithCommonFormat:AVAudioPCMFormatInt16
                     sampleRate:(double)cap->sample_rate
                       channels:(AVAudioChannelCount)cap->channels
                    interleaved:YES];

        if (!desiredFormat) {
            NSLog(@"[HAL Audio] Failed to create desired audio format");
            return -1;
        }

        /*
         * Get the hardware input format and configure a converter if needed.
         * The input tap must use the input node's native format; we convert manually.
         */
        AVAudioFormat *hwFormat = [cap->inputNode outputFormatForBus:0];
        NSLog(@"[HAL Audio] Hardware format: %@", hwFormat);

        /*
         * Install tap on input node.
         * Buffer size in frames — use ~10ms worth of samples for low latency.
         */
        AVAudioFrameCount bufferSize = (AVAudioFrameCount)(cap->sample_rate / 100); /* 10ms */

        /* Store captures in block-safe variables */
        hal_audio_capture_callback_t blockCallback = cap->callback;
        void *blockUserData = cap->user_data;
        uint32_t targetSampleRate = cap->sample_rate;
        uint32_t targetChannels = cap->channels;

        /* Compute decimation factor for resampling.
         * AVAudioEngine may deliver at hardware native rate (e.g., 48kHz on
         * simulator) even if setPreferredSampleRate was called. We decimate
         * to the target rate by taking every Nth sample. */
        double hwRate = hwFormat.sampleRate;
        uint32_t decimation = 1;
        if (hwRate > (double)targetSampleRate) {
            decimation = (uint32_t)((hwRate / (double)targetSampleRate) + 0.5);
            if (decimation < 1) decimation = 1;
        }

        [cap->inputNode installTapOnBus:0
                             bufferSize:bufferSize
                                 format:hwFormat
                                  block:^(AVAudioPCMBuffer * _Nonnull buffer,
                                          AVAudioTime * _Nonnull when) {
            if (!blockCallback) {
                return;
            }

            AVAudioFrameCount frameCount = buffer.frameLength;
            if (frameCount == 0) {
                return;
            }

            /*
             * Convert float PCM from AVAudioEngine to int16 PCM.
             * AVAudioEngine delivers float32 samples in [-1.0, 1.0] range.
             */
            const float *floatSamples = buffer.floatChannelData[0];
            if (!floatSamples) {
                return;
            }

            /* Allocate conversion buffer on stack for small buffers, heap for large */
            int16_t *int16Buffer = NULL;
            int16_t stackBuffer[4096];

            if (frameCount <= 4096) {
                int16Buffer = stackBuffer;
            } else {
                int16Buffer = (int16_t *)malloc(frameCount * sizeof(int16_t));
                if (!int16Buffer) {
                    return;
                }
            }

            /* Convert float32 → int16 with saturation */
            for (AVAudioFrameCount i = 0; i < frameCount; i++) {
                float sample = floatSamples[i];
                /* Clamp to [-1.0, 1.0] */
                if (sample > 1.0f) sample = 1.0f;
                if (sample < -1.0f) sample = -1.0f;
                int16Buffer[i] = (int16_t)(sample * 32767.0f);
            }

            /*
             * Resample by decimation if hardware rate differs from target.
             * Simple N:1 decimation (no anti-aliasing filter) — sufficient
             * for the stub VAD pipeline. A production implementation would
             * use a proper resampler (e.g., AVAudioConverter or libresample).
             */
            uint32_t outputCount = (uint32_t)frameCount;
            if (decimation > 1) {
                outputCount = (uint32_t)frameCount / decimation;
                for (uint32_t i = 0; i < outputCount; i++) {
                    int16Buffer[i] = int16Buffer[i * decimation];
                }
            }

            (void)targetChannels;

            /* Deliver resampled int16 samples via callback */
            blockCallback(int16Buffer, outputCount, blockUserData);

            /* Free heap buffer if allocated */
            if (int16Buffer != stackBuffer) {
                free(int16Buffer);
            }
        }];

        /* Start the audio engine */
        NSError *error = nil;
        [cap->engine prepare];
        [cap->engine startAndReturnError:&error];

        if (error) {
            NSLog(@"[HAL Audio] Failed to start AVAudioEngine: %@",
                  error.localizedDescription);
            [cap->inputNode removeTapOnBus:0];
            return -1;
        }

        cap->is_running = 1;
        NSLog(@"[HAL Audio] Audio capture started");
        return 0;
    }
}

void hal_audio_capture_stop(AudioCapture* cap) {
    if (!cap || !cap->is_running) {
        return;
    }

    @autoreleasepool {
        /* Remove the input tap */
        [cap->inputNode removeTapOnBus:0];

        /* Stop the engine */
        [cap->engine stop];

        cap->is_running = 0;
        cap->callback = NULL;
        cap->user_data = NULL;

        NSLog(@"[HAL Audio] Audio capture stopped");
    }
}

#endif /* TARGET_OS_IPHONE */
