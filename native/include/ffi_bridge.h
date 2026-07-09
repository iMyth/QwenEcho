/**
 * QwenEcho FFI Bridge — Public C-linkage declarations.
 *
 * These 4 functions are the only entry points exposed to the Flutter UI Shell
 * via Dart FFI. All return int32_t: 0 = success, negative = EchoErrorCode.
 */

#ifndef FFI_BRIDGE_H
#define FFI_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the QwenEcho engine with model file paths.
 *
 * Loads ASR, LLM, and TTS models from the specified paths.
 * Must be called before StartEchoPipeline.
 *
 * @param asr_path  Path to FunASR-Nano GGUF model file
 * @param llm_path  Path to Qwen3-4B-Instruct GGUF model file
 * @param tts_path  Path to Qwen3-TTS-Streaming GGUF model file
 * @return ECHO_OK (0) on success, negative EchoErrorCode on failure
 *         ECHO_ERR_ALREADY_INIT if engine is already initialized
 *         ECHO_ERR_MODEL_MISSING if any path is NULL or empty
 */
__attribute__((visibility("default")))
int32_t InitQwenEchoEngine(const char* asr_path,
                           const char* llm_path,
                           const char* tts_path);

/**
 * Start the interpretation pipeline with specified language pair.
 *
 * Begins audio capture and activates the full ASR → LLM → TTS pipeline.
 * Requires: engine initialized, no active session, port registered.
 *
 * @param source_lang  ISO 639-1 source language code (e.g., "zh")
 * @param target_lang  ISO 639-1 target language code (e.g., "en")
 * @return ECHO_OK (0) on success, negative EchoErrorCode on failure
 *         ECHO_ERR_ENGINE_NOT_READY if engine not in Ready state
 *         ECHO_ERR_SESSION_ACTIVE if pipeline already running
 *         ECHO_ERR_NO_PORT if no Native Port registered
 *         ECHO_ERR_UNSUPPORTED_LANG if language pair not supported
 */
__attribute__((visibility("default")))
int32_t StartEchoPipeline(const char* source_lang,
                          const char* target_lang);

/**
 * Stop the active interpretation pipeline.
 *
 * Ceases audio capture, processes locked segments, discards unlocked audio,
 * and releases pipeline resources.
 * Requires: port registered (for status notification).
 *
 * @return ECHO_OK (0) on success, negative EchoErrorCode on failure
 *         ECHO_ERR_NO_SESSION if no pipeline session is active
 *         ECHO_ERR_NO_PORT if no Native Port registered
 */
__attribute__((visibility("default")))
int32_t StopEchoPipeline(void);

/**
 * Initialize the Dart API DL (Dynamic Loading) subsystem.
 *
 * Must be called before any other FFI function. Pass the value of
 * NativeApi.initializeApiDLData from Dart.
 *
 * @param data  Opaque pointer from NativeApi.initializeApiDLData
 * @return ECHO_OK (0) on success
 */
__attribute__((visibility("default")))
int32_t InitDartApiDL(void* data);

/**
 * Register a Dart Native Port for async message delivery.
 *
 * Establishes the communication channel for streaming results from the
 * Engine to the UI Shell. Replaces any previously registered port.
 * Requires InitDartApiDL to have been called first.
 *
 * @param dart_port_id  The Dart SendPort ID for Native Port communication
 * @return ECHO_OK (0) on success
 */
__attribute__((visibility("default")))
int32_t RegisterEchoMessagePort(int64_t dart_port_id);

#ifdef __cplusplus
}
#endif

#endif /* FFI_BRIDGE_H */
