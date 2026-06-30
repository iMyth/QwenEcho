/**
 * Smoke test validating echo_types.h compiles and enum/struct definitions are correct.
 */

#include "echo_types.h"
#include <rapidcheck.h>

#include <cstring>

int main() {
    // Verify EngineState enum values
    rc::check("EngineState enum has correct values", []() {
        RC_ASSERT(ENGINE_UNINITIALIZED == 0);
        RC_ASSERT(ENGINE_INITIALIZING == 1);
        RC_ASSERT(ENGINE_READY == 2);
        RC_ASSERT(ENGINE_RUNNING == 3);
        RC_ASSERT(ENGINE_STOPPING == 4);
        RC_ASSERT(ENGINE_ERROR == 5);
    });

    // Verify MessageType enum values
    rc::check("MessageType enum has correct type tags", []() {
        RC_ASSERT(MSG_ASR_PARTIAL == 1);
        RC_ASSERT(MSG_ASR_CONFIRMED == 2);
        RC_ASSERT(MSG_TRANSLATION_STREAM == 3);
        RC_ASSERT(MSG_TRANSLATION_DONE == 4);
        RC_ASSERT(MSG_TTS_STARTED == 5);
        RC_ASSERT(MSG_TTS_COMPLETE == 6);
        RC_ASSERT(MSG_ERROR == 10);
        RC_ASSERT(MSG_THERMAL_STATE == 11);
        RC_ASSERT(MSG_MEMORY_WARNING == 12);
        RC_ASSERT(MSG_LATENCY_WARNING == 13);
        RC_ASSERT(MSG_SAMPLE_DROP == 14);
    });

    // Verify EchoErrorCode values
    rc::check("EchoErrorCode enum has correct values", []() {
        RC_ASSERT(ECHO_OK == 0);
        RC_ASSERT(ECHO_ERR_NOT_INITIALIZED == -1);
        RC_ASSERT(ECHO_ERR_ALREADY_INIT == -2);
        RC_ASSERT(ECHO_ERR_MODEL_MISSING == -3);
        RC_ASSERT(ECHO_ERR_MODEL_INVALID == -4);
        RC_ASSERT(ECHO_ERR_MODEL_PERMISSION == -5);
        RC_ASSERT(ECHO_ERR_MEMORY == -6);
        RC_ASSERT(ECHO_ERR_UNSUPPORTED_LANG == -7);
        RC_ASSERT(ECHO_ERR_SESSION_ACTIVE == -8);
        RC_ASSERT(ECHO_ERR_NO_SESSION == -9);
        RC_ASSERT(ECHO_ERR_NO_PORT == -10);
        RC_ASSERT(ECHO_ERR_ENGINE_NOT_READY == -11);
        RC_ASSERT(ECHO_ERR_THERMAL_CRITICAL == -12);
    });

    // Verify AsrToLlmElement struct layout
    rc::check("AsrToLlmElement text buffer is 2048 bytes", []() {
        AsrToLlmElement elem;
        RC_ASSERT(sizeof(elem.text) == 2048);
    });

    // Verify LlmToTtsElement struct layout
    rc::check("LlmToTtsElement text buffer is 4096 bytes", []() {
        LlmToTtsElement elem;
        RC_ASSERT(sizeof(elem.text) == 4096);
    });

    // Verify EngineConfig struct has expected fields
    rc::check("EngineConfig default values are assignable", []() {
        EngineConfig cfg{};
        cfg.ring_buffer_capacity = 1048576;
        cfg.throttle_temp = 43.0f;
        cfg.normal_temp = 42.0f;
        cfg.critical_temp = 50.0f;
        cfg.resume_temp = 45.0f;
        cfg.memory_warn_pct = 0.85f;
        cfg.memory_critical_pct = 0.95f;
        cfg.llm_context_normal = 512;
        cfg.llm_context_throttle = 256;
        cfg.llm_sliding_history = 3;
        cfg.silence_threshold_ms = 400;
        cfg.min_speech_ms = 200;
        cfg.max_segment_ms = 15000;
        cfg.asr_sample_rate = 16000;
        cfg.tts_sample_rate = 24000;

        RC_ASSERT(cfg.ring_buffer_capacity == 1048576);
        RC_ASSERT(cfg.asr_sample_rate == 16000);
        RC_ASSERT(cfg.tts_sample_rate == 24000);
    });

    return 0;
}
