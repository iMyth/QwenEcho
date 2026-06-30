/**
 * @file hal_thread_android.c
 * @brief Android HAL thread priority backend using pthread SCHED_FIFO.
 *
 * Implements the hal_thread.h interface for Android by setting
 * the calling thread to SCHED_FIFO real-time scheduling policy
 * with elevated priority.
 *
 * SCHED_FIFO provides:
 *   - Deterministic scheduling (no time-slicing)
 *   - Higher priority than all SCHED_OTHER threads
 *   - Preemption of normal-priority threads
 *
 * This is essential for the audio collector thread to avoid sample drops.
 *
 * Note: On Android, apps typically have permission to set SCHED_FIFO
 * with priority up to 3 without root. Higher priorities may require
 * the android.permission.SCHEDULE_EXACT_ALARM or system UID.
 */

#ifdef __ANDROID__

#include "hal_thread.h"
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <android/log.h>

#define LOG_TAG "QwenEcho_Thread"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* ─── Constants ─────────────────────────────────────────────────────────────── */

/**
 * SCHED_FIFO priority for the audio collector thread.
 *
 * Android allows priority 1-3 for non-system apps.
 * Priority 3 is the highest reliably available without system privileges.
 * The AudioFlinger service typically runs at priority 2-3.
 */
#define RT_PRIORITY_AUDIO  3

/* ─── Public HAL Interface Implementation ───────────────────────────────────── */

int hal_thread_set_realtime_priority(void) {
    pthread_t self = pthread_self();
    struct sched_param param;
    int policy;

    /* Get current scheduling parameters */
    int rc = pthread_getschedparam(self, &policy, &param);
    if (rc != 0) {
        LOGE("pthread_getschedparam failed: %s (errno=%d)", strerror(rc), rc);
        return -1;
    }

    /* Set SCHED_FIFO with elevated priority */
    param.sched_priority = RT_PRIORITY_AUDIO;
    rc = pthread_setschedparam(self, SCHED_FIFO, &param);

    if (rc != 0) {
        if (rc == EPERM) {
            /*
             * Permission denied — try a lower priority.
             * Some Android versions restrict SCHED_FIFO to priority 1-2.
             */
            LOGW("SCHED_FIFO priority %d denied, trying priority 1", RT_PRIORITY_AUDIO);
            param.sched_priority = 1;
            rc = pthread_setschedparam(self, SCHED_FIFO, &param);

            if (rc != 0) {
                /*
                 * Final fallback: Use SCHED_RR (round-robin real-time) which
                 * may have different permission requirements on some devices.
                 */
                LOGW("SCHED_FIFO priority 1 denied, trying SCHED_RR");
                param.sched_priority = 1;
                rc = pthread_setschedparam(self, SCHED_RR, &param);

                if (rc != 0) {
                    LOGE("Failed to set real-time priority: %s (errno=%d)", strerror(rc), rc);
                    return -2;
                }

                LOGI("Thread set to SCHED_RR priority 1 (fallback)");
                return 0;
            }

            LOGI("Thread set to SCHED_FIFO priority 1");
            return 0;
        }

        LOGE("pthread_setschedparam(SCHED_FIFO, %d) failed: %s (errno=%d)",
             RT_PRIORITY_AUDIO, strerror(rc), rc);
        return -3;
    }

    LOGI("Thread set to SCHED_FIFO priority %d", RT_PRIORITY_AUDIO);
    return 0;
}

#endif /* __ANDROID__ */
