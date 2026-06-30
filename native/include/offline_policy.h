/**
 * @file offline_policy.h
 * @brief Compile-time and runtime offline-only policy enforcement.
 *
 * QwenEcho operates in a strict air-gapped mode. After initial model
 * provisioning (copying GGUF files into the application sandbox), the engine
 * makes ZERO network requests — no telemetry, no analytics, no crash
 * reporting, no update checks. This header provides:
 *
 *   1. Compile-time assertions that poison known networking symbols.
 *   2. A runtime verification function callable at engine init.
 *   3. Platform-specific documentation of required manifest/plist configuration.
 *
 * ─── Platform Configuration Requirements ────────────────────────────────────
 *
 * ANDROID (AndroidManifest.xml):
 *   - MUST NOT include: <uses-permission android:name="android.permission.INTERNET"/>
 *   - MUST NOT include: <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE"/>
 *   - The engine library links ONLY against: aaudio, android, log
 *   - No OkHttp, Retrofit, Volley, or any HTTP client library is linked.
 *
 * iOS (Info.plist):
 *   - MUST NOT declare NSAppTransportSecurity exceptions
 *   - MUST NOT include NSAllowsArbitraryLoads = YES
 *   - No URLSession, CFNetwork, or socket usage in the engine code
 *   - The entitlements file SHOULD NOT include com.apple.security.network.client
 *
 * ─── Validates: Requirements 13.1, 13.2, 13.4, 13.5, 13.6 ───────────────────
 */

#ifndef OFFLINE_POLICY_H
#define OFFLINE_POLICY_H

#include "echo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * SECTION 1: Compile-Time Network Symbol Poisoning
 *
 * If any translation unit accidentally includes networking headers or calls
 * network APIs, these will trigger a hard compile error. This is a defense-
 * in-depth measure — the build system also does not link against any network
 * libraries.
 *
 * Usage: Include this header in engine source files AFTER all standard
 * library includes. Any subsequent use of poisoned identifiers will
 * produce a compile error.
 * ═══════════════════════════════════════════════════════════════════════════ */

#if defined(ECHO_ENFORCE_OFFLINE_POLICY)

#if defined(__GNUC__) || defined(__clang__)
/*
 * GCC/Clang #pragma poison: using any of these identifiers after this point
 * will trigger a hard compilation error. This is the most reliable compile-
 * time enforcement mechanism available.
 */
#pragma GCC poison socket
#pragma GCC poison connect
#pragma GCC poison bind
#pragma GCC poison listen
#pragma GCC poison accept
#pragma GCC poison send
#pragma GCC poison recv
#pragma GCC poison sendto
#pragma GCC poison recvfrom
#pragma GCC poison getaddrinfo
#pragma GCC poison gethostbyname
#pragma GCC poison gethostbyaddr
#pragma GCC poison curl_easy_init
#pragma GCC poison curl_easy_perform
#pragma GCC poison curl_easy_cleanup
#pragma GCC poison curl_global_init
#endif /* __GNUC__ || __clang__ */

#if defined(_MSC_VER)
/* MSVC: use deprecated pragma as a softer warning mechanism */
#pragma deprecated(socket, connect, bind, listen, accept, send, recv)
#endif

#endif /* ECHO_ENFORCE_OFFLINE_POLICY */

/* ═══════════════════════════════════════════════════════════════════════════
 * SECTION 2: Runtime Offline Policy Verification
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Verify that the offline-only policy is satisfied at runtime.
 *
 * This function performs the following checks:
 *   1. Confirms no network-related shared libraries are loaded by this process
 *      (on platforms where /proc/self/maps or equivalent is available).
 *   2. Validates that model files reside within the application sandbox path.
 *   3. Confirms the absence of telemetry/analytics code paths.
 *
 * @param app_sandbox_path  The application's private storage directory.
 *                          Model files must reside within this path.
 *                          - Android: Context.getFilesDir() path
 *                          - iOS: NSHomeDirectory()/Documents or Library
 * @param asr_model_path    Path to the ASR model file (must be within sandbox).
 * @param llm_model_path    Path to the LLM model file (must be within sandbox).
 * @param tts_model_path    Path to the TTS model file (must be within sandbox).
 *
 * @return ECHO_OK if all offline policy checks pass.
 *         ECHO_ERR_MODEL_PERMISSION if a model path is outside the sandbox.
 *         ECHO_ERR_NOT_INITIALIZED for other policy violations.
 */
int32_t verify_offline_policy(const char* app_sandbox_path,
                              const char* asr_model_path,
                              const char* llm_model_path,
                              const char* tts_model_path);

#ifdef __cplusplus
}
#endif

#endif /* OFFLINE_POLICY_H */
