/**
 * @file hal_memory_android.c
 * @brief Android HAL memory backend using /proc/self/statm.
 *
 * Implements the hal_memory.h interface for Android by reading
 * the process memory statistics from the procfs virtual filesystem.
 *
 * /proc/self/statm fields (in pages):
 *   1. size     - total program size (VmSize)
 *   2. resident - RSS (VmRSS)
 *   3. shared   - shared pages
 *   4. text     - text (code)
 *   5. lib      - library (unused since Linux 2.6)
 *   6. data     - data + stack
 *   7. dt       - dirty pages (unused since Linux 2.6)
 *
 * We read field 2 (resident) and multiply by page size to get RSS in bytes.
 */

#ifdef __ANDROID__

#include "hal_memory.h"
#include <stdio.h>
#include <unistd.h>
#include <android/log.h>

#define LOG_TAG "QwenEcho_Memory"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* ─── Constants ─────────────────────────────────────────────────────────────── */

/**
 * Android memory budget: 2.5 GB
 * This is the maximum RSS the engine should consume before triggering mitigation.
 */
#define ANDROID_MEMORY_LIMIT_BYTES  ((size_t)2684354560ULL)  /* 2.5 * 1024^3 */

/* ─── Public HAL Interface Implementation ───────────────────────────────────── */

size_t hal_memory_get_rss(void) {
    FILE* fp = fopen("/proc/self/statm", "r");
    if (!fp) {
        LOGE("Failed to open /proc/self/statm");
        return 0;
    }

    /*
     * Read the second field (resident pages) from /proc/self/statm.
     * Format: "size resident shared text lib data dt"
     * All values are in pages.
     */
    unsigned long vm_size = 0;
    unsigned long resident_pages = 0;

    if (fscanf(fp, "%lu %lu", &vm_size, &resident_pages) != 2) {
        LOGE("Failed to parse /proc/self/statm");
        fclose(fp);
        return 0;
    }
    fclose(fp);

    /*
     * Convert pages to bytes.
     * Page size is typically 4096 on most Android devices,
     * but may be 16384 on Android 15+ with 16KB page support.
     */
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        page_size = 4096;  /* Fallback to standard page size */
    }

    size_t rss_bytes = (size_t)resident_pages * (size_t)page_size;
    return rss_bytes;
}

size_t hal_memory_get_platform_limit(void) {
    return ANDROID_MEMORY_LIMIT_BYTES;
}

#endif /* __ANDROID__ */
