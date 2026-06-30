/**
 * @file thermal_monitor.cpp
 * @brief Thermal Monitor implementation.
 *
 * Dedicated low-priority thread polls hal_thermal_get_temperature() every
 * 5 seconds and evaluates the three-mode state machine with hysteresis:
 *
 *   Normal → Throttle   when temp > 43°C
 *   Throttle → Normal   when temp ≤ 42°C
 *   Throttle → Critical when temp > 50°C
 *   Critical → Throttle when temp ≤ 45°C
 *
 * On each transition:
 *   - Posts MSG_THERMAL_STATE via native_port_post_thermal_state()
 *   - Invokes the user callback (Engine Manager adaptation)
 */

#include "thermal_monitor.h"
#include "native_port.h"
#include "../hal/hal_thermal.h"

#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>

/* Hysteresis thresholds (Celsius) */
static constexpr float THRESHOLD_NORMAL_TO_THROTTLE = 43.0f;
static constexpr float THRESHOLD_THROTTLE_TO_NORMAL = 42.0f;
static constexpr float THRESHOLD_THROTTLE_TO_CRITICAL = 50.0f;
static constexpr float THRESHOLD_CRITICAL_TO_THROTTLE = 45.0f;

/* Polling interval */
static constexpr auto POLL_INTERVAL = std::chrono::seconds(5);

struct ThermalMonitor {
    /* Current thermal state (atomic for lock-free reads) */
    std::atomic<ThermalMode> mode{THERMAL_NORMAL};

    /* User callback for mode transitions */
    thermal_mode_callback_t callback{nullptr};
    void* user_data{nullptr};

    /* Thread control */
    std::thread poll_thread;
    std::atomic<bool> running{false};

    /* Condition variable for interruptible sleep */
    std::mutex stop_mutex;
    std::condition_variable stop_cv;
    std::atomic<bool> stop_requested{false};
};

/**
 * Evaluate the thermal state machine given the current temperature.
 * Returns true if a transition occurred.
 */
static bool evaluate_state_machine(ThermalMonitor* monitor, float temp,
                                   ThermalMode* new_mode) {
    ThermalMode current = monitor->mode.load(std::memory_order_acquire);
    ThermalMode next = current;

    switch (current) {
        case THERMAL_NORMAL:
            if (temp > THRESHOLD_NORMAL_TO_THROTTLE) {
                next = THERMAL_THROTTLE;
            }
            break;

        case THERMAL_THROTTLE:
            if (temp > THRESHOLD_THROTTLE_TO_CRITICAL) {
                next = THERMAL_CRITICAL;
            } else if (temp <= THRESHOLD_THROTTLE_TO_NORMAL) {
                next = THERMAL_NORMAL;
            }
            break;

        case THERMAL_CRITICAL:
            if (temp <= THRESHOLD_CRITICAL_TO_THROTTLE) {
                next = THERMAL_THROTTLE;
            }
            break;
    }

    if (next != current) {
        monitor->mode.store(next, std::memory_order_release);
        *new_mode = next;
        return true;
    }
    return false;
}

/**
 * Polling thread entry point.
 * Runs at default (non-RT) priority — we intentionally do NOT elevate
 * this thread since thermal monitoring is a background, low-priority task.
 */
static void thermal_poll_loop(ThermalMonitor* monitor) {
    while (!monitor->stop_requested.load(std::memory_order_acquire)) {
        /* Read temperature from HAL */
        float temp = hal_thermal_get_temperature();

        /* Skip evaluation if HAL returned an error */
        if (temp >= 0.0f) {
            ThermalMode new_mode;
            if (evaluate_state_machine(monitor, temp, &new_mode)) {
                /* Notify UI Shell via Native Port */
                native_port_post_thermal_state(
                    static_cast<int32_t>(new_mode),
                    static_cast<double>(temp));

                /* Invoke Engine Manager callback */
                if (monitor->callback) {
                    monitor->callback(new_mode, monitor->user_data);
                }
            }
        }

        /* Interruptible sleep: wait for POLL_INTERVAL or stop signal */
        {
            std::unique_lock<std::mutex> lock(monitor->stop_mutex);
            monitor->stop_cv.wait_for(lock, POLL_INTERVAL, [monitor]() {
                return monitor->stop_requested.load(std::memory_order_acquire);
            });
        }
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

extern "C" {

ThermalMonitor* thermal_monitor_create(thermal_mode_callback_t callback,
                                       void* user_data) {
    ThermalMonitor* monitor = new (std::nothrow) ThermalMonitor();
    if (!monitor) {
        return nullptr;
    }
    monitor->callback = callback;
    monitor->user_data = user_data;
    return monitor;
}

void thermal_monitor_start(ThermalMonitor* monitor) {
    if (!monitor) return;

    /* Already running — no-op */
    if (monitor->running.load(std::memory_order_acquire)) return;

    monitor->stop_requested.store(false, std::memory_order_release);
    monitor->running.store(true, std::memory_order_release);
    monitor->poll_thread = std::thread(thermal_poll_loop, monitor);
}

void thermal_monitor_stop(ThermalMonitor* monitor) {
    if (!monitor) return;

    if (!monitor->running.load(std::memory_order_acquire)) return;

    /* Signal the polling thread to exit */
    monitor->stop_requested.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(monitor->stop_mutex);
        monitor->stop_cv.notify_one();
    }

    /* Wait for thread to finish */
    if (monitor->poll_thread.joinable()) {
        monitor->poll_thread.join();
    }
    monitor->running.store(false, std::memory_order_release);
}

ThermalMode thermal_monitor_get_mode(const ThermalMonitor* monitor) {
    if (!monitor) return THERMAL_NORMAL;
    return monitor->mode.load(std::memory_order_acquire);
}

void thermal_monitor_destroy(ThermalMonitor* monitor) {
    if (!monitor) return;

    thermal_monitor_stop(monitor);
    delete monitor;
}

} /* extern "C" */
