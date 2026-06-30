/**
 * QwenEcho Memory Monitor — Implementation.
 *
 * Spawns a low-priority background thread that samples process RSS every
 * 2 seconds via the Memory HAL. Implements two-level mitigation with
 * upward-only hysteresis:
 *
 *   Level 1 (85%): invoke user callback with MEM_LEVEL_WARNING
 *   Level 2 (95%): invoke user callback with MEM_LEVEL_CRITICAL,
 *                   also post MSG_MEMORY_WARNING via Native Port
 *
 * The monitor thread runs at default (normal) scheduling priority — no
 * real-time elevation — keeping CPU impact minimal.
 */

#include "memory_monitor.h"
#include "native_port.h"

extern "C" {
#include "hal_memory.h"
}

#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>

// ---------------------------------------------------------------------------
// Internal structure
// ---------------------------------------------------------------------------

struct MemoryMonitor {
    // User callback and context
    memory_action_callback_t callback;
    void*                    user_data;

    // Current level (atomic for lock-free reads from any thread)
    std::atomic<MemoryLevel> current_level{MEM_LEVEL_OK};

    // Thread control
    std::atomic<bool>        running{false};
    std::thread              worker;
    std::mutex               mutex;
    std::condition_variable  cv;

    // Thresholds (fraction of platform limit)
    static constexpr float kWarningThreshold  = 0.85f;
    static constexpr float kCriticalThreshold = 0.95f;

    // Poll interval
    static constexpr std::chrono::milliseconds kPollInterval{2000};
};

// ---------------------------------------------------------------------------
// Worker thread function
// ---------------------------------------------------------------------------

static void memory_monitor_thread_fn(MemoryMonitor* mon)
{
    // Get platform limit once (does not change at runtime)
    const size_t limit = hal_memory_get_platform_limit();
    if (limit == 0) {
        // Cannot monitor without a valid limit
        return;
    }

    const size_t warning_threshold  = static_cast<size_t>(
        static_cast<double>(limit) * MemoryMonitor::kWarningThreshold);
    const size_t critical_threshold = static_cast<size_t>(
        static_cast<double>(limit) * MemoryMonitor::kCriticalThreshold);

    while (mon->running.load(std::memory_order_acquire)) {
        // Sample current RSS
        const size_t rss = hal_memory_get_rss();

        // Determine target level
        MemoryLevel target;
        if (rss >= critical_threshold) {
            target = MEM_LEVEL_CRITICAL;
        } else if (rss >= warning_threshold) {
            target = MEM_LEVEL_WARNING;
        } else {
            target = MEM_LEVEL_OK;
        }

        // Hysteresis: only transition upward (never fire the same level twice,
        // and never downgrade — once at WARNING, stay there until critical or
        // until the monitor is restarted).
        MemoryLevel current = mon->current_level.load(std::memory_order_acquire);
        if (target > current) {
            mon->current_level.store(target, std::memory_order_release);

            // Invoke user callback
            if (mon->callback) {
                mon->callback(target, rss, limit, mon->user_data);
            }

            // At critical level, also notify UI via Native Port
            if (target == MEM_LEVEL_CRITICAL) {
                native_port_post_memory_warning(
                    static_cast<int64_t>(rss),
                    static_cast<int64_t>(limit),
                    static_cast<int32_t>(target));
            }
        }

        // Sleep for poll interval, but wake early if stop is requested
        {
            std::unique_lock<std::mutex> lock(mon->mutex);
            mon->cv.wait_for(lock, MemoryMonitor::kPollInterval, [mon]() {
                return !mon->running.load(std::memory_order_acquire);
            });
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

extern "C" {

MemoryMonitor* memory_monitor_create(memory_action_callback_t callback,
                                     void* user_data)
{
    MemoryMonitor* mon = new (std::nothrow) MemoryMonitor();
    if (!mon) {
        return nullptr;
    }
    mon->callback  = callback;
    mon->user_data = user_data;
    return mon;
}

void memory_monitor_start(MemoryMonitor* monitor)
{
    if (!monitor) return;

    // No-op if already running
    if (monitor->running.load(std::memory_order_acquire)) {
        return;
    }

    monitor->running.store(true, std::memory_order_release);
    monitor->current_level.store(MEM_LEVEL_OK, std::memory_order_release);
    monitor->worker = std::thread(memory_monitor_thread_fn, monitor);
}

void memory_monitor_stop(MemoryMonitor* monitor)
{
    if (!monitor) return;

    if (!monitor->running.load(std::memory_order_acquire)) {
        return;
    }

    // Signal thread to exit
    monitor->running.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(monitor->mutex);
        monitor->cv.notify_one();
    }

    // Wait for thread to finish
    if (monitor->worker.joinable()) {
        monitor->worker.join();
    }
}

MemoryLevel memory_monitor_get_level(const MemoryMonitor* monitor)
{
    if (!monitor) return MEM_LEVEL_OK;
    return monitor->current_level.load(std::memory_order_acquire);
}

void memory_monitor_destroy(MemoryMonitor* monitor)
{
    if (!monitor) return;

    // Ensure thread is stopped before destruction
    memory_monitor_stop(monitor);
    delete monitor;
}

} // extern "C"
