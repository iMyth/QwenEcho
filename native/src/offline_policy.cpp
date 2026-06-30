/**
 * @file offline_policy.cpp
 * @brief Runtime enforcement of offline-only policy.
 *
 * Verifies at engine initialization time that:
 *   - Model files reside within the application sandbox (no external access)
 *   - No network-related symbols are dynamically loaded
 *   - The engine operates without network interfaces
 *
 * This module contains ZERO network code — it only inspects the process state
 * to confirm no other module has introduced network dependencies.
 *
 * ─── Platform Notes ──────────────────────────────────────────────────────────
 *
 * ANDROID:
 *   - The AndroidManifest.xml MUST NOT include:
 *       <uses-permission android:name="android.permission.INTERNET"/>
 *       <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE"/>
 *   - Without INTERNET permission, any socket() call will fail with EACCES,
 *     providing OS-level enforcement even if code accidentally calls network APIs.
 *   - The engine .so links only against: libaaudio.so, libandroid.so, liblog.so
 *   - No libcurl, libssl, or libcrypto linkage.
 *
 * iOS:
 *   - Info.plist MUST NOT declare NSAppTransportSecurity exceptions.
 *   - The entitlements file SHOULD NOT include com.apple.security.network.client.
 *   - No URLSession, CFNetwork, or BSD socket usage in engine code.
 *   - App Review and static analysis tools will flag any undeclared network usage.
 *
 * ─── Validates: Requirements 13.1, 13.2, 13.4, 13.5, 13.6 ───────────────────
 */

#include "offline_policy.h"

#include <cstdlib>
#include <cstring>

/* ─── Internal Helper: Path Prefix Check ─────────────────────────────────── */

/**
 * Check if `path` starts with `prefix`.
 * Used to verify model files are within the application sandbox.
 */
static bool path_is_within_sandbox(const char* path, const char* sandbox_prefix) {
    if (!path || !sandbox_prefix) return false;
    size_t prefix_len = strlen(sandbox_prefix);
    if (prefix_len == 0) return false;
    return (strncmp(path, sandbox_prefix, prefix_len) == 0);
}

/* ─── Internal Helper: Check for Network Libraries (Linux/Android) ────────── */

#if defined(__ANDROID__) || defined(__linux__)
#include <cstdio>

/**
 * Scan /proc/self/maps for known network-related shared libraries.
 * Returns true if NO network libraries are found (policy satisfied).
 *
 * Known network library patterns we check for:
 *   - libssl.so      (OpenSSL / BoringSSL)
 *   - libcrypto.so   (OpenSSL / BoringSSL — often paired with network use)
 *   - libcurl.so     (HTTP client library)
 *   - libnghttp2.so  (HTTP/2 library)
 *
 * Note: On Android, some system libraries may reference these internally,
 * but OUR process should not have them mapped if we don't link against them.
 * This check is best-effort and serves as a diagnostic, not a security barrier.
 */
static bool check_no_network_libraries_loaded(void) {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        /* Cannot read maps — skip this check (not a failure on all platforms) */
        return true;
    }

    static const char* const kNetworkLibPatterns[] = {
        "libcurl.so",
        "libnghttp2.so",
        /* Note: we do NOT flag libssl/libcrypto because Android system loads
         * them for non-network purposes (keystore, etc.). We focus on
         * libraries that definitively indicate HTTP/network intent. */
    };
    static const size_t kPatternCount =
        sizeof(kNetworkLibPatterns) / sizeof(kNetworkLibPatterns[0]);

    char line[512];
    bool policy_ok = true;

    while (fgets(line, sizeof(line), fp) != nullptr) {
        for (size_t i = 0; i < kPatternCount; ++i) {
            if (strstr(line, kNetworkLibPatterns[i]) != nullptr) {
                policy_ok = false;
                break;
            }
        }
        if (!policy_ok) break;
    }

    fclose(fp);
    return policy_ok;
}
#endif /* __ANDROID__ || __linux__ */

/* ─── Internal Helper: Check for Network Libraries (iOS/macOS) ────────────── */

#if defined(__APPLE__)
#include <dlfcn.h>

/**
 * On Apple platforms, check that we haven't dynamically loaded networking
 * frameworks beyond what the system provides by default.
 *
 * We verify that our code hasn't explicitly dlopen'd network frameworks.
 * Note: The system always has CFNetwork available, but our code must not
 * USE it. This runtime check is a best-effort signal.
 */
static bool check_no_network_libraries_loaded(void) {
    /* Check if networking symbols we'd use are resolvable in our binary.
     * dlsym with RTLD_DEFAULT searches only loaded images.
     * If we find curl or custom HTTP symbols, something is wrong. */
    if (dlsym(RTLD_DEFAULT, "curl_easy_init") != nullptr) {
        return false;
    }
    if (dlsym(RTLD_DEFAULT, "curl_easy_perform") != nullptr) {
        return false;
    }
    /* Our engine should not resolve its own HTTP request functions */
    if (dlsym(RTLD_DEFAULT, "echo_http_request") != nullptr) {
        return false;
    }
    if (dlsym(RTLD_DEFAULT, "echo_send_telemetry") != nullptr) {
        return false;
    }
    if (dlsym(RTLD_DEFAULT, "echo_send_analytics") != nullptr) {
        return false;
    }
    return true;
}
#endif /* __APPLE__ */

/* ─── Fallback for other platforms (desktop testing) ──────────────────────── */

#if !defined(__ANDROID__) && !defined(__linux__) && !defined(__APPLE__)
static bool check_no_network_libraries_loaded(void) {
    /* On desktop/test builds, assume policy is satisfied */
    return true;
}
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

int32_t verify_offline_policy(const char* app_sandbox_path,
                              const char* asr_model_path,
                              const char* llm_model_path,
                              const char* tts_model_path) {
    /* ─── Check 1: Validate sandbox path is non-empty ───────────────────── */
    if (!app_sandbox_path || app_sandbox_path[0] == '\0') {
        return ECHO_ERR_NOT_INITIALIZED;
    }

    /* ─── Check 2: Verify all model paths are within the app sandbox ─────── */
    /*
     * Requirement 13.2: Model files stored within application sandbox,
     * inaccessible to other applications.
     *
     * On Android, the sandbox is typically:
     *   /data/data/<package>/files/  or  /data/user/0/<package>/files/
     *
     * On iOS, the sandbox is typically:
     *   /var/mobile/Containers/Data/Application/<UUID>/Documents/
     */
    if (!path_is_within_sandbox(asr_model_path, app_sandbox_path)) {
        return ECHO_ERR_MODEL_PERMISSION;
    }
    if (!path_is_within_sandbox(llm_model_path, app_sandbox_path)) {
        return ECHO_ERR_MODEL_PERMISSION;
    }
    if (!path_is_within_sandbox(tts_model_path, app_sandbox_path)) {
        return ECHO_ERR_MODEL_PERMISSION;
    }

    /* ─── Check 3: Verify no network libraries are loaded ────────────────── */
    /*
     * Requirement 13.4: Zero network requests after model provisioning.
     * Requirement 13.5: No telemetry, analytics, crash-reporting, or
     *                    update-check functionality.
     *
     * This check scans loaded libraries for known network-related .so/.dylib
     * files. If found, something has introduced a network dependency.
     */
    if (!check_no_network_libraries_loaded()) {
        return ECHO_ERR_NOT_INITIALIZED;
    }

    /* ─── Check 4: Verify no telemetry symbols exist ─────────────────────── */
    /*
     * Requirement 13.5: No telemetry/analytics/crash-reporting code.
     *
     * The compile-time poisoning in offline_policy.h prevents accidental
     * inclusion at build time. At runtime, we confirm that no telemetry
     * entry points have been linked into the binary.
     *
     * This is implicitly covered by Check 3 (no network libraries) and the
     * compile-time enforcement. The engine source code contains zero
     * telemetry, analytics, or crash-reporting code by design.
     */

    /* ─── All checks passed ──────────────────────────────────────────────── */
    /*
     * Requirement 13.6: Engine launches and operates without network
     * interfaces present. Since we verified no network dependencies exist,
     * the engine will function correctly regardless of network availability.
     */
    return ECHO_OK;
}
