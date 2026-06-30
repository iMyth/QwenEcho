/**
 * @file hal_thread.h
 * @brief Thread priority HAL interface.
 *
 * Abstracts real-time thread priority setting across platforms:
 *   - Android: pthread_setschedparam with SCHED_FIFO
 *   - iOS: pthread_set_qos_class_self_np with QOS_CLASS_USER_INTERACTIVE
 */

#ifndef HAL_THREAD_H
#define HAL_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Set the calling thread to real-time priority.
 *
 * Elevates the current thread's scheduling priority to the platform's
 * real-time or highest-priority class, suitable for audio capture threads.
 *
 * On Android, this sets SCHED_FIFO policy with elevated priority.
 * On iOS, this sets QOS_CLASS_USER_INTERACTIVE via pthread QoS APIs.
 *
 * @return 0 on success, negative error code on failure (e.g. insufficient permissions).
 */
int hal_thread_set_realtime_priority(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_THREAD_H */
