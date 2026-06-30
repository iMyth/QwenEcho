#ifndef ECHO_TYPES_H
#define ECHO_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Engine lifecycle states.
 * Transitions: Uninitialized → Initializing → Ready → Running → Stopping → Ready
 *              Initializing → Error (on load failure)
 *              Error → Uninitialized (on reset)
 */
typedef enum {
    ENGINE_UNINITIALIZED = 0,
    ENGINE_INITIALIZING,
    ENGINE_READY,
    ENGINE_RUNNING,
    ENGINE_STOPPING,
    ENGINE_ERROR
} EngineState;

/**
 * Native Port message type tags.
 * Each message sent from Engine to UI Shell begins with one of these tags.
 */
typedef enum {
    MSG_ASR_PARTIAL        = 1,   /* Temporary/unconfirmed ASR text */
    MSG_ASR_CONFIRMED      = 2,   /* Finalized ASR text with punctuation */
    MSG_TRANSLATION_STREAM = 3,   /* Streaming translation token */
    MSG_TRANSLATION_DONE   = 4,   /* Translation segment complete */
    MSG_TTS_STARTED        = 5,   /* TTS synthesis began for a segment */
    MSG_TTS_COMPLETE       = 6,   /* TTS synthesis finished for a segment */
    MSG_ERROR              = 10,  /* Error notification */
    MSG_THERMAL_STATE      = 11,  /* Thermal mode change */
    MSG_MEMORY_WARNING     = 12,  /* Memory pressure event */
    MSG_LATENCY_WARNING    = 13,  /* SLA violation */
    MSG_SAMPLE_DROP        = 14   /* Audio sample drop detected */
} MessageType;

/**
 * FFI error codes returned by all C-linkage entry points.
 * 0 = success, negative = error.
 */
typedef enum {
    ECHO_OK                    =   0,
    ECHO_ERR_NOT_INITIALIZED   =  -1,
    ECHO_ERR_ALREADY_INIT      =  -2,
    ECHO_ERR_MODEL_MISSING     =  -3,
    ECHO_ERR_MODEL_INVALID     =  -4,
    ECHO_ERR_MODEL_PERMISSION  =  -5,
    ECHO_ERR_MEMORY            =  -6,
    ECHO_ERR_UNSUPPORTED_LANG  =  -7,
    ECHO_ERR_SESSION_ACTIVE    =  -8,
    ECHO_ERR_NO_SESSION        =  -9,
    ECHO_ERR_NO_PORT           = -10,
    ECHO_ERR_ENGINE_NOT_READY  = -11,
    ECHO_ERR_THERMAL_CRITICAL  = -12
} EchoErrorCode;

/**
 * ASR → LLM inter-stage queue element.
 * Produced by ASR on sentence confirmation, consumed by LLM translation stage.
 */
typedef struct {
    uint32_t segment_id;
    uint8_t  speaker_id;      /* 0 = speaker A (bottom), 1 = speaker B (top) */
    char     text[2048];      /* UTF-8 confirmed text (null-terminated) */
    uint16_t text_len;
    uint64_t timestamp_ms;    /* Segment lock timestamp */
} AsrToLlmElement;

/**
 * LLM → TTS inter-stage queue element.
 * Produced by LLM on translation output, consumed by TTS synthesis stage.
 */
typedef struct {
    uint32_t segment_id;
    uint8_t  speaker_id;
    char     text[4096];      /* UTF-8 translated text (null-terminated) */
    uint16_t text_len;
    uint64_t timestamp_ms;
} LlmToTtsElement;

/**
 * Engine configuration supplied at initialization.
 * All paths and strings must remain valid for the lifetime of the engine.
 */
typedef struct {
    /* Model paths */
    const char* asr_model_path;
    const char* llm_model_path;
    const char* tts_model_path;

    /* Pipeline parameters */
    const char* source_lang;          /* ISO 639-1 */
    const char* target_lang;          /* ISO 639-1 */

    /* Ring buffer */
    uint32_t ring_buffer_capacity;    /* Default: 1048576 (2^20) */

    /* Thermal thresholds (Celsius) */
    float throttle_temp;              /* Default: 43.0 */
    float normal_temp;                /* Default: 42.0 */
    float critical_temp;              /* Default: 50.0 */
    float resume_temp;                /* Default: 45.0 */

    /* Memory limits (bytes) */
    size_t memory_limit;              /* Platform-specific */
    float memory_warn_pct;            /* Default: 0.85 */
    float memory_critical_pct;        /* Default: 0.95 */

    /* LLM context */
    uint32_t llm_context_normal;      /* Default: 512 */
    uint32_t llm_context_throttle;    /* Default: 256 */
    uint32_t llm_sliding_history;     /* Default: 3 (previous translations) */

    /* Sentence segmenter */
    uint32_t silence_threshold_ms;    /* Default: 400 */
    uint32_t min_speech_ms;           /* Default: 200 */
    uint32_t max_segment_ms;          /* Default: 15000 */

    /* Audio */
    uint32_t asr_sample_rate;         /* 16000 (Normal), 8000 (Throttle) */
    uint32_t tts_sample_rate;         /* 24000 */
} EngineConfig;

#ifdef __cplusplus
}
#endif

#endif /* ECHO_TYPES_H */
