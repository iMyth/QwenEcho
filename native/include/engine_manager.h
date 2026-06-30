/**
 * @file engine_manager.h
 * @brief Engine Manager — central coordinator for engine lifecycle,
 *        model loading, and pipeline orchestration.
 *
 * State machine:
 *   Uninitialized → Initializing → Ready → Running → Stopping → Ready
 *   Initializing → Error (on load failure)
 *   Error → Uninitialized (on reset/destroy)
 *
 * Guards:
 *   - load_models when not Uninitialized → ECHO_ERR_ALREADY_INIT
 *   - start_pipeline when not Ready → ECHO_ERR_ENGINE_NOT_READY
 *   - start_pipeline when session active → ECHO_ERR_SESSION_ACTIVE
 *   - stop_pipeline when no session → ECHO_OK (no-op)
 */

#ifndef ENGINE_MANAGER_H
#define ENGINE_MANAGER_H

#include "echo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque Engine Manager handle.
 */
typedef struct EngineManager EngineManager;

/**
 * Create a new Engine Manager instance in the Uninitialized state.
 *
 * @return Pointer to the new EngineManager, or NULL on allocation failure.
 */
EngineManager* engine_manager_create(void);

/**
 * Load all three models (ASR, LLM, TTS) and transition to Ready state.
 *
 * Transitions: Uninitialized → Initializing → Ready (success)
 *              Uninitialized → Initializing → Error (failure)
 *
 * @param em        Engine Manager instance.
 * @param asr_path  Path to FunASR-Nano GGUF model file.
 * @param llm_path  Path to Qwen3-4B-Instruct GGUF model file.
 * @param tts_path  Path to Qwen3-TTS-Streaming GGUF model file.
 * @return ECHO_OK on success, negative EchoErrorCode on failure.
 *         ECHO_ERR_ALREADY_INIT if not in Uninitialized state.
 *         ECHO_ERR_MODEL_MISSING if any path is NULL or empty.
 */
int engine_manager_load_models(EngineManager* em, const char* asr_path,
                               const char* llm_path, const char* tts_path);

/**
 * Start the interpretation pipeline with the given language pair.
 *
 * Transitions: Ready → Running
 *
 * @param em        Engine Manager instance.
 * @param src_lang  ISO 639-1 source language code.
 * @param tgt_lang  ISO 639-1 target language code.
 * @return ECHO_OK on success, negative EchoErrorCode on failure.
 *         ECHO_ERR_ENGINE_NOT_READY if not in Ready state.
 *         ECHO_ERR_SESSION_ACTIVE if a pipeline session is already active.
 *         ECHO_ERR_UNSUPPORTED_LANG if language codes are invalid.
 */
int engine_manager_start_pipeline(EngineManager* em, const char* src_lang,
                                  const char* tgt_lang);

/**
 * Stop the active pipeline and return to Ready state.
 *
 * Transitions: Running → Stopping → Ready
 * If no session is active (state is Ready), returns ECHO_OK as a no-op.
 *
 * @param em  Engine Manager instance.
 * @return ECHO_OK on success.
 */
int engine_manager_stop_pipeline(EngineManager* em);

/**
 * Destroy the Engine Manager and release all resources.
 * Unloads models and frees the manager. NULL is safely ignored.
 *
 * @param em  Engine Manager instance to destroy.
 */
void engine_manager_destroy(EngineManager* em);

/**
 * Query the current engine state.
 *
 * @param em  Engine Manager instance (may be NULL).
 * @return Current EngineState, or ENGINE_UNINITIALIZED if em is NULL.
 */
EngineState engine_manager_get_state(const EngineManager* em);

#ifdef __cplusplus
}
#endif

#endif /* ENGINE_MANAGER_H */
