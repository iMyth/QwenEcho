/**
 * QwenEcho Native Port — Implementation.
 *
 * Serializes typed messages as Dart_CObject arrays and dispatches them
 * via the registered Dart Native Port. Uses atomic operations for the
 * port state since pipeline threads may post messages concurrently.
 */

/* Include real Dart SDK headers FIRST so native_port.h skips its shim types */
#include "dart_api_dl.h"
#include "native_port.h"
#include "echo_types.h"

#include <atomic>
#include <cstring>

#ifdef __APPLE__
#include <os/log.h>
#define ECHO_LOG(fmt, ...) os_log(OS_LOG_DEFAULT, fmt, ##__VA_ARGS__)
#else
#define ECHO_LOG(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#endif

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------

namespace {

/** The currently registered Dart port. 0 = unregistered. */
std::atomic<int64_t> g_port_id{0};

/** Whether a port has been registered at least once. */
std::atomic<bool> g_port_registered{false};

/** Stored function pointer — uses Dart_PostCObject_DL by default.
 *  Tests can override via native_port_set_post_fn(). */
typedef bool (*PostFn)(int64_t, Dart_CObject*);
std::atomic<PostFn> g_post_fn{nullptr};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Port registration
// ---------------------------------------------------------------------------

extern "C" {

void native_port_register(int64_t port_id)
{
    ECHO_LOG("[NativePort] Registering port: %lld", (long long)port_id);
    g_port_id.store(port_id, std::memory_order_release);
    g_port_registered.store(true, std::memory_order_release);
}

bool native_port_is_registered(void)
{
    return g_port_registered.load(std::memory_order_acquire);
}

void native_port_set_post_fn(void)
{
    /* Default: use Dart_PostCObject_DL from the initialized Dart API DL */
    g_post_fn.store(reinterpret_cast<PostFn>(Dart_PostCObject_DL),
                     std::memory_order_release);
    ECHO_LOG("[NativePort] Post function set: Dart_PostCObject_DL=%p",
             (void*)Dart_PostCObject_DL);
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/**
 * Post a fully-constructed Dart_CObject to the registered port.
 * Uses the stored function pointer (defaults to Dart_PostCObject_DL).
 */
static bool post_message(Dart_CObject* message)
{
    if (!g_port_registered.load(std::memory_order_acquire)) {
        ECHO_LOG("[NativePort] post_message FAILED: no port registered");
        return false;
    }

    PostFn fn = g_post_fn.load(std::memory_order_acquire);
    if (fn == nullptr) {
        ECHO_LOG("[NativePort] post_message FAILED: post_fn is null");
        return false;
    }

    int64_t port = g_port_id.load(std::memory_order_acquire);
    bool ok = fn(port, message);
    if (!ok) {
        ECHO_LOG("[NativePort] post_message FAILED: Dart_PostCObject returned false");
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Helper: initialize a Dart_CObject as various types
// ---------------------------------------------------------------------------

static inline void cobject_set_int32(Dart_CObject* obj, int32_t val)
{
    obj->type = Dart_CObject_kInt32;
    obj->value.as_int32 = val;
}

static inline void cobject_set_int64(Dart_CObject* obj, int64_t val)
{
    obj->type = Dart_CObject_kInt64;
    obj->value.as_int64 = val;
}

static inline void cobject_set_double(Dart_CObject* obj, double val)
{
    obj->type = Dart_CObject_kDouble;
    obj->value.as_double = val;
}

static inline void cobject_set_string(Dart_CObject* obj, const char* val)
{
    obj->type = Dart_CObject_kString;
    obj->value.as_string = val;
}

static inline void cobject_set_array(Dart_CObject* obj, Dart_CObject** elements, int length)
{
    obj->type = Dart_CObject_kArray;
    obj->value.as_array.length = length;
    obj->value.as_array.values = elements;
}

// ---------------------------------------------------------------------------
// Typed message dispatch
// ---------------------------------------------------------------------------

bool native_port_post_asr_partial(uint8_t speaker_id,
                                  const char* text,
                                  uint64_t timestamp_ms)
{
    // [type, speaker_id, text, timestamp_ms]
    Dart_CObject c_type, c_speaker, c_text, c_timestamp;
    cobject_set_int32(&c_type, MSG_ASR_PARTIAL);
    cobject_set_int32(&c_speaker, speaker_id);
    cobject_set_string(&c_text, text);
    cobject_set_int64(&c_timestamp, static_cast<int64_t>(timestamp_ms));

    Dart_CObject* elements[] = {&c_type, &c_speaker, &c_text, &c_timestamp};

    Dart_CObject message;
    cobject_set_array(&message, elements, 4);

    return post_message(&message);
}

bool native_port_post_asr_confirmed(uint8_t speaker_id,
                                    const char* text,
                                    uint64_t timestamp_ms,
                                    uint32_t segment_id)
{
    // [type, speaker_id, text, timestamp_ms, segment_id]
    Dart_CObject c_type, c_speaker, c_text, c_timestamp, c_segment;
    cobject_set_int32(&c_type, MSG_ASR_CONFIRMED);
    cobject_set_int32(&c_speaker, speaker_id);
    cobject_set_string(&c_text, text);
    cobject_set_int64(&c_timestamp, static_cast<int64_t>(timestamp_ms));
    cobject_set_int32(&c_segment, static_cast<int32_t>(segment_id));

    Dart_CObject* elements[] = {&c_type, &c_speaker, &c_text, &c_timestamp, &c_segment};

    Dart_CObject message;
    cobject_set_array(&message, elements, 5);

    return post_message(&message);
}

bool native_port_post_translation_stream(uint8_t speaker_id,
                                         const char* token,
                                         uint32_t segment_id)
{
    // [type, speaker_id, token, segment_id]
    Dart_CObject c_type, c_speaker, c_token, c_segment;
    cobject_set_int32(&c_type, MSG_TRANSLATION_STREAM);
    cobject_set_int32(&c_speaker, speaker_id);
    cobject_set_string(&c_token, token);
    cobject_set_int32(&c_segment, static_cast<int32_t>(segment_id));

    Dart_CObject* elements[] = {&c_type, &c_speaker, &c_token, &c_segment};

    Dart_CObject message;
    cobject_set_array(&message, elements, 4);

    return post_message(&message);
}

bool native_port_post_translation_done(uint8_t speaker_id,
                                       const char* full_text,
                                       uint32_t segment_id)
{
    // [type, speaker_id, full_text, segment_id]
    Dart_CObject c_type, c_speaker, c_text, c_segment;
    cobject_set_int32(&c_type, MSG_TRANSLATION_DONE);
    cobject_set_int32(&c_speaker, speaker_id);
    cobject_set_string(&c_text, full_text);
    cobject_set_int32(&c_segment, static_cast<int32_t>(segment_id));

    Dart_CObject* elements[] = {&c_type, &c_speaker, &c_text, &c_segment};

    Dart_CObject message;
    cobject_set_array(&message, elements, 4);

    return post_message(&message);
}

bool native_port_post_tts_started(uint8_t speaker_id,
                                  uint32_t segment_id)
{
    // [type, speaker_id, segment_id]
    Dart_CObject c_type, c_speaker, c_segment;
    cobject_set_int32(&c_type, MSG_TTS_STARTED);
    cobject_set_int32(&c_speaker, speaker_id);
    cobject_set_int32(&c_segment, static_cast<int32_t>(segment_id));

    Dart_CObject* elements[] = {&c_type, &c_speaker, &c_segment};

    Dart_CObject message;
    cobject_set_array(&message, elements, 3);

    return post_message(&message);
}

bool native_port_post_tts_complete(uint8_t speaker_id,
                                   uint32_t segment_id)
{
    // [type, speaker_id, segment_id]
    Dart_CObject c_type, c_speaker, c_segment;
    cobject_set_int32(&c_type, MSG_TTS_COMPLETE);
    cobject_set_int32(&c_speaker, speaker_id);
    cobject_set_int32(&c_segment, static_cast<int32_t>(segment_id));

    Dart_CObject* elements[] = {&c_type, &c_speaker, &c_segment};

    Dart_CObject message;
    cobject_set_array(&message, elements, 3);

    return post_message(&message);
}

bool native_port_post_error(int32_t error_code,
                            const char* model_name,
                            const char* detail)
{
    // [type, error_code, model_name, detail_string]
    Dart_CObject c_type, c_error, c_model, c_detail;
    cobject_set_int32(&c_type, MSG_ERROR);
    cobject_set_int32(&c_error, error_code);
    cobject_set_string(&c_model, model_name);
    cobject_set_string(&c_detail, detail);

    Dart_CObject* elements[] = {&c_type, &c_error, &c_model, &c_detail};

    Dart_CObject message;
    cobject_set_array(&message, elements, 4);

    return post_message(&message);
}

bool native_port_post_thermal_state(int32_t thermal_mode,
                                    double temperature_c)
{
    // [type, thermal_mode, temperature_c]
    Dart_CObject c_type, c_mode, c_temp;
    cobject_set_int32(&c_type, MSG_THERMAL_STATE);
    cobject_set_int32(&c_mode, thermal_mode);
    cobject_set_double(&c_temp, temperature_c);

    Dart_CObject* elements[] = {&c_type, &c_mode, &c_temp};

    Dart_CObject message;
    cobject_set_array(&message, elements, 3);

    return post_message(&message);
}

bool native_port_post_memory_warning(int64_t current_bytes,
                                     int64_t limit_bytes,
                                     int32_t level)
{
    // [type, current_bytes, limit_bytes, level]
    Dart_CObject c_type, c_current, c_limit, c_level;
    cobject_set_int32(&c_type, MSG_MEMORY_WARNING);
    cobject_set_int64(&c_current, current_bytes);
    cobject_set_int64(&c_limit, limit_bytes);
    cobject_set_int32(&c_level, level);

    Dart_CObject* elements[] = {&c_type, &c_current, &c_limit, &c_level};

    Dart_CObject message;
    cobject_set_array(&message, elements, 4);

    return post_message(&message);
}

bool native_port_post_latency_warning(const char* stage,
                                      int32_t actual_ms,
                                      int32_t budget_ms)
{
    // [type, stage, actual_ms, budget_ms]
    Dart_CObject c_type, c_stage, c_actual, c_budget;
    cobject_set_int32(&c_type, MSG_LATENCY_WARNING);
    cobject_set_string(&c_stage, stage);
    cobject_set_int32(&c_actual, actual_ms);
    cobject_set_int32(&c_budget, budget_ms);

    Dart_CObject* elements[] = {&c_type, &c_stage, &c_actual, &c_budget};

    Dart_CObject message;
    cobject_set_array(&message, elements, 4);

    return post_message(&message);
}

bool native_port_post_sample_drop(int32_t dropped_samples,
                                  uint64_t timestamp_ms)
{
    // [type, dropped_samples, timestamp_ms]
    Dart_CObject c_type, c_dropped, c_timestamp;
    cobject_set_int32(&c_type, MSG_SAMPLE_DROP);
    cobject_set_int32(&c_dropped, dropped_samples);
    cobject_set_int64(&c_timestamp, static_cast<int64_t>(timestamp_ms));

    Dart_CObject* elements[] = {&c_type, &c_dropped, &c_timestamp};

    Dart_CObject message;
    cobject_set_array(&message, elements, 3);

    return post_message(&message);
}

} // extern "C"
