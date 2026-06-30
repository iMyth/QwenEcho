/**
 * @file hal_thermal_android.c
 * @brief Android HAL thermal backend using AThermal API.
 *
 * Implements the hal_thermal.h interface for Android using:
 *   - AThermal API (API level 30+) for thermal headroom
 *   - Fallback to sysfs thermal zones for older devices
 *
 * The AThermal API provides thermal headroom as a float (0.0 to 1.0+),
 * which we convert to an approximate temperature for the thermal state machine.
 *
 * Requires Android NDK r21+ and targets Android 11+ (API level 30+).
 */

#ifdef __ANDROID__

#include "hal_thermal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dlfcn.h>
#include <pthread.h>
#include <android/log.h>
#include <android/thermal.h>

#define LOG_TAG "QwenEcho_Thermal"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* ─── Constants ─────────────────────────────────────────────────────────────── */

/**
 * Conversion from thermal headroom to approximate Celsius.
 *
 * AThermal_getThermalHeadroom returns a forecast of thermal headroom:
 *   0.0  → device is cool (maps to ~25°C)
 *   0.7  → moderate load (maps to ~40°C)
 *   1.0  → thermal throttling imminent (maps to ~45°C)
 *   >1.0 → device is overheating (maps to ~50°C+)
 *
 * We use a linear approximation: temp ≈ 25 + headroom * 30
 * Clamped to [20°C, 65°C] for safety.
 */
#define HEADROOM_BASE_TEMP  25.0f
#define HEADROOM_SCALE      30.0f
#define TEMP_MIN            20.0f
#define TEMP_MAX            65.0f

/* Sysfs fallback path for CPU thermal zone */
#define SYSFS_THERMAL_ZONE  "/sys/class/thermal/thermal_zone0/temp"

/* ─── AThermal API Types ────────────────────────────────────────────────────── */

typedef struct AThermalManager AThermalManager;

typedef struct {
    void* lib_handle;
    AThermalManager* (*AThermal_acquireManager)(void);
    void (*AThermal_releaseManager)(AThermalManager* manager);
    float (*AThermal_getThermalHeadroom)(AThermalManager* manager, int forecastSeconds);
    int available;
} AThermalApi;

/* ─── Module State ──────────────────────────────────────────────────────────── */

static AThermalApi g_thermal_api = {0};
static AThermalManager* g_thermal_manager = NULL;
static hal_thermal_callback_t g_thermal_callback = NULL;
static void* g_thermal_user = NULL;
static pthread_mutex_t g_thermal_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ─── Sysfs Fallback ────────────────────────────────────────────────────────── */

/**
 * Read temperature from sysfs thermal zone.
 * Returns temperature in Celsius, or -1.0f on failure.
 * sysfs reports temperature in millidegrees Celsius (e.g., 42000 = 42.0°C).
 */
static float read_sysfs_temperature(void) {
    FILE* fp = fopen(SYSFS_THERMAL_ZONE, "r");
    if (!fp) {
        return -1.0f;
    }

    int temp_millicelsius = 0;
    if (fscanf(fp, "%d", &temp_millicelsius) != 1) {
        fclose(fp);
        return -1.0f;
    }
    fclose(fp);

    return (float)temp_millicelsius / 1000.0f;
}

/* ─── AThermal API Initialization ───────────────────────────────────────────── */

/**
 * Initialize AThermal API via dlopen. This allows graceful fallback
 * on devices where the API is not available.
 */
static void thermal_api_init(void) {
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;

    memset(&g_thermal_api, 0, sizeof(AThermalApi));

    /* AThermal functions are in libandroid.so on API 30+ */
    g_thermal_api.lib_handle = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (!g_thermal_api.lib_handle) {
        LOGW("AThermal: libandroid.so not available, using sysfs fallback");
        return;
    }

    g_thermal_api.AThermal_acquireManager =
        dlsym(g_thermal_api.lib_handle, "AThermal_acquireManager");
    g_thermal_api.AThermal_releaseManager =
        dlsym(g_thermal_api.lib_handle, "AThermal_releaseManager");
    g_thermal_api.AThermal_getThermalHeadroom =
        dlsym(g_thermal_api.lib_handle, "AThermal_getThermalHeadroom");

    if (!g_thermal_api.AThermal_acquireManager ||
        !g_thermal_api.AThermal_releaseManager ||
        !g_thermal_api.AThermal_getThermalHeadroom) {
        LOGW("AThermal: API functions not found (pre-API 30?), using sysfs fallback");
        dlclose(g_thermal_api.lib_handle);
        g_thermal_api.lib_handle = NULL;
        return;
    }

    g_thermal_manager = g_thermal_api.AThermal_acquireManager();
    if (!g_thermal_manager) {
        LOGW("AThermal: acquireManager returned NULL, using sysfs fallback");
        dlclose(g_thermal_api.lib_handle);
        g_thermal_api.lib_handle = NULL;
        return;
    }

    g_thermal_api.available = 1;
    LOGI("AThermal: initialized successfully");
}

/**
 * Convert thermal headroom value to approximate Celsius.
 */
static float headroom_to_celsius(float headroom) {
    float temp = HEADROOM_BASE_TEMP + headroom * HEADROOM_SCALE;

    /* Clamp to reasonable range */
    if (temp < TEMP_MIN) temp = TEMP_MIN;
    if (temp > TEMP_MAX) temp = TEMP_MAX;

    return temp;
}

/* ─── Public HAL Interface Implementation ───────────────────────────────────── */

float hal_thermal_get_temperature(void) {
    thermal_api_init();

    if (g_thermal_api.available && g_thermal_manager) {
        /*
         * Request thermal headroom forecast for the next 10 seconds.
         * This gives us a forward-looking estimate useful for throttling decisions.
         */
        float headroom = g_thermal_api.AThermal_getThermalHeadroom(g_thermal_manager, 10);

        if (headroom < 0.0f) {
            /* Negative headroom indicates the API returned an error */
            LOGW("AThermal: getThermalHeadroom returned %f, falling back to sysfs", headroom);
            return read_sysfs_temperature();
        }

        float temp = headroom_to_celsius(headroom);
        return temp;
    }

    /* Fallback: read from sysfs thermal zone */
    return read_sysfs_temperature();
}

int hal_thermal_register_callback(hal_thermal_callback_t cb, void* user) {
    if (!cb) return -1;

    thermal_api_init();

    pthread_mutex_lock(&g_thermal_mutex);
    g_thermal_callback = cb;
    g_thermal_user = user;
    pthread_mutex_unlock(&g_thermal_mutex);

    /*
     * Note: On Android 11+, we could use AThermal_registerThermalStatusListener
     * for push-based notifications. For QwenEcho, the Thermal Monitor thread
     * polls hal_thermal_get_temperature() every 5 seconds, so the callback
     * is invoked from that polling loop rather than from a system listener.
     *
     * The callback registration here stores the function pointer for the
     * polling thread to invoke when it detects temperature changes.
     */
    LOGI("Thermal callback registered");
    return 0;
}

#endif /* __ANDROID__ */
