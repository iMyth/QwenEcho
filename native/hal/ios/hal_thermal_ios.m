/**
 * @file hal_thermal_ios.m
 * @brief iOS thermal monitoring HAL backend using ProcessInfo.thermalState.
 *
 * Implements the hal_thermal.h interface for iOS:
 *   - Polls NSProcessInfo.thermalState and maps to approximate Celsius values
 *   - Supports registering a callback for thermal state change notifications
 *   - Uses NSProcessInfoThermalStateDidChangeNotification for reactive updates
 */

#if TARGET_OS_IPHONE

#import <Foundation/Foundation.h>

#include "../hal_thermal.h"
#include <stdlib.h>

/**
 * Maps NSProcessInfoThermalState to approximate Celsius temperature.
 *
 * iOS does not expose exact temperature readings. We map the four thermal
 * states to representative temperatures that align with the thermal state
 * machine thresholds defined in the design:
 *   - Normal → Throttle: temp > 43°C
 *   - Throttle → Normal: temp ≤ 42°C
 *   - Throttle → Critical: temp > 50°C
 *   - Critical → Throttle: temp ≤ 45°C
 */
static float thermal_state_to_celsius(NSProcessInfoThermalState state) {
    switch (state) {
        case NSProcessInfoThermalStateNominal:
            return 35.0f;  /* Well below throttle threshold */
        case NSProcessInfoThermalStateFair:
            return 41.0f;  /* Below throttle, but warming */
        case NSProcessInfoThermalStateSerious:
            return 45.0f;  /* Above throttle threshold (>43°C) */
        case NSProcessInfoThermalStateCritical:
            return 52.0f;  /* Above critical threshold (>50°C) */
        default:
            return -1.0f;  /* Unknown state */
    }
}

#pragma mark - Thermal Polling

float hal_thermal_get_temperature(void) {
    @autoreleasepool {
        NSProcessInfoThermalState state = [[NSProcessInfo processInfo] thermalState];
        return thermal_state_to_celsius(state);
    }
}

#pragma mark - Callback Registration

/**
 * Internal storage for the registered thermal callback.
 * Only one callback is supported at a time (matching HAL interface semantics).
 */
static hal_thermal_callback_t g_thermal_callback = NULL;
static void *g_thermal_user_data = NULL;
static id g_thermal_observer = nil;

/**
 * Handler for NSProcessInfoThermalStateDidChangeNotification.
 */
static void thermal_state_did_change(NSNotification *notification) {
    if (!g_thermal_callback) {
        return;
    }

    NSProcessInfoThermalState state = [[NSProcessInfo processInfo] thermalState];
    float temperature = thermal_state_to_celsius(state);

    NSLog(@"[HAL Thermal] State changed: %ld → %.1f°C",
          (long)state, temperature);

    /* Invoke registered callback */
    g_thermal_callback(temperature, g_thermal_user_data);
}

int hal_thermal_register_callback(hal_thermal_callback_t cb, void* user) {
    if (!cb) {
        return -1;
    }

    @autoreleasepool {
        /* Remove previous observer if one exists */
        if (g_thermal_observer) {
            [[NSNotificationCenter defaultCenter] removeObserver:g_thermal_observer];
            g_thermal_observer = nil;
        }

        /* Store callback info */
        g_thermal_callback = cb;
        g_thermal_user_data = user;

        /* Register for thermal state change notifications */
        g_thermal_observer = [[NSNotificationCenter defaultCenter]
            addObserverForName:NSProcessInfoThermalStateDidChangeNotification
                       object:nil
                        queue:nil /* Deliver on posting thread */
                   usingBlock:^(NSNotification * _Nonnull notification) {
                       thermal_state_did_change(notification);
                   }];

        NSLog(@"[HAL Thermal] Callback registered, current state: %ld",
              (long)[[NSProcessInfo processInfo] thermalState]);
        return 0;
    }
}

#endif /* TARGET_OS_IPHONE */
