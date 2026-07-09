/**
 * QwenEcho FFI Bridge — Implementation.
 *
 * Delegates all lifecycle operations to the Engine Manager. Maintains only
 * port registration state locally, since the port is an FFI-layer concern
 * (the Engine Manager doesn't know about Dart ports).
 */

#include "ffi_bridge.h"
#include "dart_api_dl.h"
#include "echo_types.h"
#include "engine_manager.h"
#include "native_port.h"

#include <atomic>
#include <cstring>
#include <mutex>

#ifdef __APPLE__
#include <os/log.h>
#define ECHO_LOG(fmt, ...) os_log(OS_LOG_DEFAULT, fmt, ##__VA_ARGS__)
#else
#define ECHO_LOG(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#endif

// ---------------------------------------------------------------------------
// File-scoped engine state singleton
// ---------------------------------------------------------------------------

namespace {

/**
 * Global FFI context holding the Engine Manager instance and port registration.
 * The Engine Manager owns all lifecycle state; the FFI bridge only tracks port.
 */
struct FFIContext {
    std::mutex              mutex;
    EngineManager*          engine_manager{nullptr};
    std::atomic<int64_t>    registered_port{0};
    std::atomic<bool>       port_registered{false};
};

static FFIContext g_ffi_ctx;

/**
 * Ensure the global Engine Manager is created (lazy initialization).
 * Must be called under g_ffi_ctx.mutex.
 */
static EngineManager* ensure_engine_manager() {
    if (!g_ffi_ctx.engine_manager) {
        g_ffi_ctx.engine_manager = engine_manager_create();
    }
    return g_ffi_ctx.engine_manager;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// FFI Entry Points
// ---------------------------------------------------------------------------

extern "C" {

__attribute__((visibility("default")))
int32_t InitQwenEchoEngine(const char* asr_path,
                           const char* llm_path,
                           const char* tts_path)
{
    std::lock_guard<std::mutex> lock(g_ffi_ctx.mutex);

    EngineManager* em = ensure_engine_manager();
    if (!em) {
        return ECHO_ERR_MEMORY;
    }

    return engine_manager_load_models(em, asr_path, llm_path, tts_path);
}

__attribute__((visibility("default")))
int32_t StartEchoPipeline(const char* source_lang,
                          const char* target_lang)
{
    std::lock_guard<std::mutex> lock(g_ffi_ctx.mutex);

    EngineManager* em = g_ffi_ctx.engine_manager;
    if (!em) {
        return ECHO_ERR_NOT_INITIALIZED;
    }

    /* Guard: port must be registered before starting pipeline */
    if (!g_ffi_ctx.port_registered.load(std::memory_order_acquire)) {
        return ECHO_ERR_NO_PORT;
    }

    return engine_manager_start_pipeline(em, source_lang, target_lang);
}

__attribute__((visibility("default")))
int32_t StopEchoPipeline(void)
{
    std::lock_guard<std::mutex> lock(g_ffi_ctx.mutex);

    EngineManager* em = g_ffi_ctx.engine_manager;
    if (!em) {
        return ECHO_ERR_NOT_INITIALIZED;
    }

    /* Guard: port must be registered for status notifications */
    if (!g_ffi_ctx.port_registered.load(std::memory_order_acquire)) {
        return ECHO_ERR_NO_PORT;
    }

    return engine_manager_stop_pipeline(em);
}

__attribute__((visibility("default")))
int32_t InitDartApiDL(void* data)
{
    intptr_t result = Dart_InitializeApiDL(data);
    if (result != 0) {
        ECHO_LOG("[FFI] Dart_InitializeApiDL failed: %ld", (long)result);
        return ECHO_ERR_NOT_INITIALIZED;
    }
    ECHO_LOG("[FFI] Dart API DL initialized, Dart_PostCObject_DL=%p",
             (void*)Dart_PostCObject_DL);
    return ECHO_OK;
}

__attribute__((visibility("default")))
int32_t RegisterEchoMessagePort(int64_t dart_port_id)
{
    std::lock_guard<std::mutex> lock(g_ffi_ctx.mutex);

    /* Store the port, replacing any previously registered port */
    g_ffi_ctx.registered_port.store(dart_port_id, std::memory_order_release);
    g_ffi_ctx.port_registered.store(true, std::memory_order_release);

    /* Mark post function as ready — Dart_PostCObject_DL is already
     * initialized by Dart_InitializeApiDL called from Dart side */
    native_port_set_post_fn();

    /* Forward to native_port module for message dispatch */
    native_port_register(dart_port_id);

    return ECHO_OK;
}

} // extern "C"
