/**
 * @file hal_memory_ios.c
 * @brief iOS memory monitoring HAL backend using task_info TASK_VM_INFO.
 *
 * Implements the hal_memory.h interface for iOS:
 *   - Reads process RSS via mach task_info with TASK_VM_INFO flavor
 *   - Returns platform memory budget limit of 2.0 GB for iOS
 *
 * Pure C implementation — no Objective-C required.
 */

#ifdef __APPLE__

#include "../hal_memory.h"

#include <mach/mach.h>
#include <mach/task.h>
#include <mach/task_info.h>
#include <stddef.h>

/**
 * iOS memory budget limit: 2.0 GB (2,147,483,648 bytes).
 *
 * This conservative limit accounts for the system's memory pressure
 * management. iOS will terminate apps exceeding their memory allowance
 * without warning via Jetsam.
 */
#define IOS_MEMORY_LIMIT_BYTES ((size_t)2147483648ULL) /* 2.0 GB */

size_t hal_memory_get_rss(void) {
    task_vm_info_data_t vm_info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    kern_return_t kr;

    kr = task_info(mach_task_self(),
                   TASK_VM_INFO,
                   (task_info_t)&vm_info,
                   &count);

    if (kr != KERN_SUCCESS) {
        return 0;
    }

    /*
     * phys_footprint is the recommended metric on iOS.
     * It represents the actual physical memory used by the process,
     * accounting for compressed and purgeable memory. This is the
     * same metric used by Jetsam for memory limit enforcement.
     */
    return (size_t)vm_info.phys_footprint;
}

size_t hal_memory_get_platform_limit(void) {
    return IOS_MEMORY_LIMIT_BYTES;
}

#endif /* __APPLE__ */
