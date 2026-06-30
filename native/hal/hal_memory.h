/**
 * @file hal_memory.h
 * @brief Memory monitoring HAL interface.
 *
 * Abstracts process memory reading across platforms:
 *   - Android: /proc/self/statm
 *   - iOS: task_info TASK_VM_INFO
 */

#ifndef HAL_MEMORY_H
#define HAL_MEMORY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the current process Resident Set Size (RSS).
 *
 * Returns the physical memory currently used by the process.
 *
 * @return RSS in bytes, or 0 on failure.
 */
size_t hal_memory_get_rss(void);

/**
 * Get the platform-specific memory budget limit.
 *
 * Returns the maximum memory the engine should consume:
 *   - Android: 2.5 GB (2,684,354,560 bytes)
 *   - iOS: 2.0 GB (2,147,483,648 bytes)
 *
 * @return Memory limit in bytes.
 */
size_t hal_memory_get_platform_limit(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_MEMORY_H */
