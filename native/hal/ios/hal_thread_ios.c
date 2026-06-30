/**
 * @file hal_thread_ios.c
 * @brief iOS thread priority HAL backend using pthread QoS.
 *
 * Implements the hal_thread.h interface for iOS:
 *   - Sets QOS_CLASS_USER_INTERACTIVE for real-time thread priority
 *   - Uses pthread_set_qos_class_self_np (non-portable Apple extension)
 *
 * Pure C implementation — no Objective-C required.
 */

#ifdef __APPLE__

#include "../hal_thread.h"

#include <pthread.h>
#include <pthread/qos.h>
#include <sys/qos.h>

int hal_thread_set_realtime_priority(void) {
    /*
     * QOS_CLASS_USER_INTERACTIVE is the highest QoS class available to
     * regular applications on iOS. It signals to the scheduler that this
     * thread is doing time-critical, user-facing work (audio capture).
     *
     * The relative priority of 0 gives us the default priority within
     * the QoS class. Values range from 0 (highest) to -15 (lowest)
     * within a QoS class.
     */
    int ret = pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);

    if (ret != 0) {
        /*
         * This can fail if:
         *   - The thread is already in a workgroup incompatible with QoS
         *   - The process doesn't have the required entitlement
         * Return the error code as a negative value per HAL convention.
         */
        return -ret;
    }

    return 0;
}

#endif /* __APPLE__ */
