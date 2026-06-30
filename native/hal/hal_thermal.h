/**
 * @file hal_thermal.h
 * @brief Thermal monitoring HAL interface.
 *
 * Abstracts hardware temperature reading across platforms:
 *   - Android: AThermal API / sysfs thermal zones
 *   - iOS: ProcessInfo.thermalState
 */

#ifndef HAL_THERMAL_H
#define HAL_THERMAL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Thermal state callback function type.
 *
 * @param temp  Current temperature in degrees Celsius.
 * @param user  User-provided context pointer.
 */
typedef void (*hal_thermal_callback_t)(float temp, void* user);

/**
 * Get the current device temperature.
 *
 * Polls the platform thermal sensor and returns the temperature in Celsius.
 * On platforms that provide thermal headroom rather than absolute temperature,
 * the value is converted to an approximate Celsius reading.
 *
 * @return Temperature in degrees Celsius, or -1.0f on failure.
 */
float hal_thermal_get_temperature(void);

/**
 * Register a callback for thermal state changes.
 *
 * The callback will be invoked when the platform reports a thermal state
 * change or when the polling interval detects a significant temperature shift.
 *
 * @param cb    Callback function (must not be NULL).
 * @param user  User context pointer passed to the callback.
 * @return 0 on success, negative error code on failure.
 */
int hal_thermal_register_callback(hal_thermal_callback_t cb, void* user);

#ifdef __cplusplus
}
#endif

#endif /* HAL_THERMAL_H */
