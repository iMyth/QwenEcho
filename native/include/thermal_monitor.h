/**
 * @file thermal_monitor.h
 * @brief Thermal Monitor — polls hardware temperature and drives
 *        the three-mode thermal state machine with hysteresis.
 *
 * State machine:
 *   Normal → Throttle  when temp > 43°C
 *   Throttle → Normal  when temp ≤ 42°C
 *   Throttle → Critical when temp > 50°C
 *   Critical → Throttle when temp ≤ 45°C
 *
 * On every transition the monitor:
 *   1. Sends MSG_THERMAL_STATE to the UI Shell via Native Port
 *   2. Invokes the user-supplied callback for Engine Manager adaptation
 *
 * The monitor runs on a dedicated low-priority thread, polling every 5 seconds.
 */

#ifndef THERMAL_MONITOR_H
#define THERMAL_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Thermal operating modes.
 */
typedef enum {
    THERMAL_NORMAL   = 0,
    THERMAL_THROTTLE = 1,
    THERMAL_CRITICAL = 2,
} ThermalMode;

/**
 * Callback invoked on each thermal mode transition.
 *
 * @param mode  The new thermal mode after the transition.
 * @param user  User-provided context pointer (typically EngineManager*).
 */
typedef void (*thermal_mode_callback_t)(ThermalMode mode, void* user);

/**
 * Opaque Thermal Monitor handle.
 */
typedef struct ThermalMonitor ThermalMonitor;

/**
 * Create a Thermal Monitor instance.
 *
 * The monitor is created in a stopped state. Call thermal_monitor_start()
 * to begin the polling loop.
 *
 * @param callback   Function called on every thermal mode transition.
 *                   May be NULL if only Native Port notification is desired.
 * @param user_data  Context pointer passed to the callback.
 * @return Pointer to the new ThermalMonitor, or NULL on allocation failure.
 */
ThermalMonitor* thermal_monitor_create(thermal_mode_callback_t callback,
                                       void* user_data);

/**
 * Start the thermal monitoring thread.
 *
 * Spawns a low-priority thread that polls hal_thermal_get_temperature()
 * every 5 seconds and evaluates the thermal state machine.
 *
 * If the monitor is already running, this is a no-op.
 *
 * @param monitor  Thermal Monitor instance.
 */
void thermal_monitor_start(ThermalMonitor* monitor);

/**
 * Stop the thermal monitoring thread.
 *
 * Signals the polling thread to exit and joins it. Safe to call even
 * if the monitor is not running (no-op).
 *
 * @param monitor  Thermal Monitor instance.
 */
void thermal_monitor_stop(ThermalMonitor* monitor);

/**
 * Query the current thermal mode.
 *
 * Thread-safe — may be called from any thread.
 *
 * @param monitor  Thermal Monitor instance (may be NULL).
 * @return Current ThermalMode, or THERMAL_NORMAL if monitor is NULL.
 */
ThermalMode thermal_monitor_get_mode(const ThermalMonitor* monitor);

/**
 * Destroy the Thermal Monitor and release all resources.
 *
 * If the monitor is still running, it will be stopped first.
 * NULL is safely ignored.
 *
 * @param monitor  Thermal Monitor instance to destroy.
 */
void thermal_monitor_destroy(ThermalMonitor* monitor);

#ifdef __cplusplus
}
#endif

#endif /* THERMAL_MONITOR_H */
