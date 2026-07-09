/**
 * QwenEcho Native Port — Public Interface.
 *
 * Provides typed message dispatch functions for sending structured messages
 * from the C/C++ Engine to the Flutter UI Shell via Dart Native Port.
 *
 * Each post function serializes its payload as a Dart_CObject array and
 * dispatches it through the registered port using Dart_PostCObject_DL (or
 * a runtime-set function pointer when the Dart SDK header is unavailable).
 */

#ifndef NATIVE_PORT_H
#define NATIVE_PORT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Dart_CObject compatibility shim.
 *
 * When building without the Dart SDK (dart_api_dl.h), we define the
 * minimal subset of Dart_CObject types needed for message serialization.
 * If dart_api_dl.h IS available, include it and skip this block.
 * ----------------------------------------------------------------------- */
#if !defined(DART_API_DL_H_) && !defined(RUNTIME_INCLUDE_DART_NATIVE_API_H_)

typedef enum {
    Dart_CObject_kNull    = 0,
    Dart_CObject_kBool    = 1,
    Dart_CObject_kInt32   = 2,
    Dart_CObject_kInt64   = 3,
    Dart_CObject_kDouble  = 4,
    Dart_CObject_kString  = 5,
    Dart_CObject_kArray   = 6,
    /* Additional types exist but are unused by NativePort */
} Dart_CObject_Type;

typedef struct _Dart_CObject {
    Dart_CObject_Type type;
    union {
        bool          as_bool;
        int32_t       as_int32;
        int64_t       as_int64;
        double        as_double;
        const char*   as_string;
        struct {
            int                    length;
            struct _Dart_CObject** values;
        } as_array;
    } value;
} Dart_CObject;

/* Dart port identifier */
typedef int64_t Dart_Port;

/* Function signature for posting a CObject to a Dart port */
typedef bool (*Dart_PostCObject_Type)(Dart_Port port_id, Dart_CObject* message);

#endif /* DART_API_DL_H_ */

/* -----------------------------------------------------------------------
 * Port registration and lifecycle
 * ----------------------------------------------------------------------- */

/**
 * Register (or replace) the Dart Native Port for message delivery.
 *
 * Only the most recently registered port receives messages. Calling this
 * function with a new port_id silently replaces the previous registration.
 *
 * @param port_id  The Dart SendPort ID from RegisterEchoMessagePort
 */
void native_port_register(int64_t port_id);

/**
 * Check whether a Native Port has been registered.
 *
 * @return true if a port is registered, false otherwise
 */
bool native_port_is_registered(void);

/**
 * Mark the post function as ready.
 *
 * Call after Dart_InitializeApiDL has been invoked so that
 * Dart_PostCObject_DL is available for message dispatch.
 */
void native_port_set_post_fn(void);

/* -----------------------------------------------------------------------
 * Typed message dispatch functions
 * ----------------------------------------------------------------------- */

/**
 * Post MSG_ASR_PARTIAL: [type, speaker_id, text, timestamp_ms]
 */
bool native_port_post_asr_partial(uint8_t speaker_id,
                                  const char* text,
                                  uint64_t timestamp_ms);

/**
 * Post MSG_ASR_CONFIRMED: [type, speaker_id, text, timestamp_ms, segment_id]
 */
bool native_port_post_asr_confirmed(uint8_t speaker_id,
                                    const char* text,
                                    uint64_t timestamp_ms,
                                    uint32_t segment_id);

/**
 * Post MSG_TRANSLATION_STREAM: [type, speaker_id, token, segment_id]
 */
bool native_port_post_translation_stream(uint8_t speaker_id,
                                         const char* token,
                                         uint32_t segment_id);

/**
 * Post MSG_TRANSLATION_DONE: [type, speaker_id, full_text, segment_id]
 */
bool native_port_post_translation_done(uint8_t speaker_id,
                                       const char* full_text,
                                       uint32_t segment_id);

/**
 * Post MSG_TTS_STARTED: [type, speaker_id, segment_id]
 */
bool native_port_post_tts_started(uint8_t speaker_id,
                                  uint32_t segment_id);

/**
 * Post MSG_TTS_COMPLETE: [type, speaker_id, segment_id]
 */
bool native_port_post_tts_complete(uint8_t speaker_id,
                                   uint32_t segment_id);

/**
 * Post MSG_ERROR: [type, error_code, model_name, detail_string]
 */
bool native_port_post_error(int32_t error_code,
                            const char* model_name,
                            const char* detail);

/**
 * Post MSG_THERMAL_STATE: [type, thermal_mode, temperature_c]
 */
bool native_port_post_thermal_state(int32_t thermal_mode,
                                    double temperature_c);

/**
 * Post MSG_MEMORY_WARNING: [type, current_bytes, limit_bytes, level]
 */
bool native_port_post_memory_warning(int64_t current_bytes,
                                     int64_t limit_bytes,
                                     int32_t level);

/**
 * Post MSG_LATENCY_WARNING: [type, stage, actual_ms, budget_ms]
 */
bool native_port_post_latency_warning(const char* stage,
                                      int32_t actual_ms,
                                      int32_t budget_ms);

/**
 * Post MSG_SAMPLE_DROP: [type, dropped_samples, timestamp_ms]
 */
bool native_port_post_sample_drop(int32_t dropped_samples,
                                  uint64_t timestamp_ms);

#ifdef __cplusplus
}
#endif

#endif /* NATIVE_PORT_H */
