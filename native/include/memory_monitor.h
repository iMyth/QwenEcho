/**
 * @file memory_monitor.h
 * @brief Memory Monitor — Public Interface.
 *
 * Periodically samples process RSS via HAL and triggers two-level
 * mitigation when memory usage approaches platform limits:
 *   - Level 1 (85%): release LLM KV caches + TTS output buffers
 *   - Level 2 (95%): graceful pipeline stop + MSG_MEMORY_WARNING to UI
 *
 * Platform limits: 2.5 GB Android, 2.0 GB iOS (obtained from HAL).
 */

#ifndef MEMORY_MONITOR_H
#define MEMORY_MONITOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Memory pressure levels.
 */
typedef enum {
    MEM_LEVEL_OK       = 0,   /**< Below 85% — no action required */
    MEM_LEVEL_WARNING  = 1,   /**< 85% — release caches */
    MEM_LEVEL_CRITICAL = 2,   /**< 95% — stop pipeline */
} MemoryLevel;

/**
 * Callback invoked when a memory level transition occurs.
 *
 * @param level         The new memory pressure level.
 * @param current_bytes Current process RSS in bytes.
 * @param limit_bytes   Platform memory budget in bytes.
 * @param user          User-provided context pointer.
 */
typedef void (*memory_action_callback_t)(MemoryLevel level,
                                         size_t current_bytes,
                                         size_t limit_bytes,
                                         void* user);

/**
 * Opaque memory monitor handle.
 */
typedef struct MemoryMonitor MemoryMonitor;

/**
 * Create a Memory Monitor instance.
 *
 * The callback is invoked on level transitions (upward only due to hysteresis).
 * The monitor does NOT start polling until memory_monitor_start() is called.
 *
 * @param callback   Function to call on level transitions (may be NULL for testing).
 * @param user_data  Opaque pointer forwarded to callback.
 * @return           Monitor handle, or NULL on allocation failure.
 */
MemoryMonitor* memory_monitor_create(memory_action_callback_t callback,
                                     void* user_data);

/**
 * Start the memory monitoring thread.
 *
 * Spawns a low-priority background thread that samples RSS every 2 seconds.
 * Calling start on an already-running monitor is a no-op.
 *
 * @param monitor  Monitor handle (must not be NULL).
 */
void memory_monitor_start(MemoryMonitor* monitor);

/**
 * Stop the memory monitoring thread.
 *
 * Signals the monitoring thread to exit and joins it. Safe to call if
 * the monitor is not running (no-op). After stop, the monitor can be
 * restarted with memory_monitor_start().
 *
 * @param monitor  Monitor handle (must not be NULL).
 */
void memory_monitor_stop(MemoryMonitor* monitor);

/**
 * Get the current memory pressure level.
 *
 * Thread-safe; may be called from any thread.
 *
 * @param monitor  Monitor handle (must not be NULL).
 * @return         Current MemoryLevel.
 */
MemoryLevel memory_monitor_get_level(const MemoryMonitor* monitor);

/**
 * Destroy the Memory Monitor and free all resources.
 *
 * Stops the monitoring thread if running, then deallocates. After this
 * call, the monitor pointer is invalid.
 *
 * @param monitor  Monitor handle (NULL is safe — no-op).
 */
void memory_monitor_destroy(MemoryMonitor* monitor);

#ifdef __cplusplus
}
#endif

#endif /* MEMORY_MONITOR_H */
