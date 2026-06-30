/**
 * @file engine_manager.cpp
 * @brief Engine Manager implementation — state machine and lifecycle control.
 *
 * Owns a ModelLoader instance for GGUF model management, a PipelineController
 * for pipeline orchestration, and tracks engine state transitions.
 */

#include "engine_manager.h"
#include "model_loader.h"
#include "pipeline_controller.h"

#include <cstdlib>
#include <cstring>
#include <mutex>

/* ─── EngineManager struct definition ────────────────────────────────────── */

struct EngineManager {
    std::mutex           mutex;              /* Protects state transitions */
    EngineState          state;             /* Current lifecycle state */
    bool                 session_active;    /* True while pipeline is running */
    ModelLoader*         model_loader;      /* Owned model loader instance */
    PipelineController*  pipeline_ctrl;     /* Owned pipeline controller */
};

/* ─── Public API ─────────────────────────────────────────────────────────── */

EngineManager* engine_manager_create(void) {
    auto* em = static_cast<EngineManager*>(calloc(1, sizeof(EngineManager)));
    if (!em) return nullptr;

    /* Placement-new the mutex (calloc gives raw memory) */
    new (&em->mutex) std::mutex();

    em->state = ENGINE_UNINITIALIZED;
    em->session_active = false;
    em->model_loader = nullptr;
    em->pipeline_ctrl = nullptr;

    return em;
}

int engine_manager_load_models(EngineManager* em, const char* asr_path,
                               const char* llm_path, const char* tts_path) {
    if (!em) return ECHO_ERR_NOT_INITIALIZED;

    std::lock_guard<std::mutex> lock(em->mutex);

    /* Guard: must be Uninitialized to load models */
    if (em->state != ENGINE_UNINITIALIZED) {
        return ECHO_ERR_ALREADY_INIT;
    }

    /* Validate inputs: all paths must be non-null and non-empty */
    if (!asr_path || asr_path[0] == '\0') return ECHO_ERR_MODEL_MISSING;
    if (!llm_path || llm_path[0] == '\0') return ECHO_ERR_MODEL_MISSING;
    if (!tts_path || tts_path[0] == '\0') return ECHO_ERR_MODEL_MISSING;

    /* Transition to Initializing */
    em->state = ENGINE_INITIALIZING;

    /* Create the model loader */
    em->model_loader = model_loader_create();
    if (!em->model_loader) {
        em->state = ENGINE_ERROR;
        return ECHO_ERR_MEMORY;
    }

    /* Load ASR model */
    int rc = model_loader_load(em->model_loader, asr_path, MODEL_TYPE_ASR);
    if (rc != ECHO_OK) {
        model_loader_destroy(em->model_loader);
        em->model_loader = nullptr;
        em->state = ENGINE_ERROR;
        return rc;
    }

    /* Load LLM model */
    rc = model_loader_load(em->model_loader, llm_path, MODEL_TYPE_LLM);
    if (rc != ECHO_OK) {
        model_loader_destroy(em->model_loader);
        em->model_loader = nullptr;
        em->state = ENGINE_ERROR;
        return rc;
    }

    /* Load TTS model */
    rc = model_loader_load(em->model_loader, tts_path, MODEL_TYPE_TTS);
    if (rc != ECHO_OK) {
        model_loader_destroy(em->model_loader);
        em->model_loader = nullptr;
        em->state = ENGINE_ERROR;
        return rc;
    }

    /* All models loaded successfully → Ready */
    em->state = ENGINE_READY;
    return ECHO_OK;
}

int engine_manager_start_pipeline(EngineManager* em, const char* src_lang,
                                  const char* tgt_lang) {
    if (!em) return ECHO_ERR_NOT_INITIALIZED;

    std::lock_guard<std::mutex> lock(em->mutex);

    /* Guard: engine must be in Ready state */
    if (em->state != ENGINE_READY) {
        return ECHO_ERR_ENGINE_NOT_READY;
    }

    /* Guard: no duplicate session */
    if (em->session_active) {
        return ECHO_ERR_SESSION_ACTIVE;
    }

    /* Validate language inputs */
    if (!src_lang || src_lang[0] == '\0') return ECHO_ERR_UNSUPPORTED_LANG;
    if (!tgt_lang || tgt_lang[0] == '\0') return ECHO_ERR_UNSUPPORTED_LANG;

    /* Create pipeline controller if not already available */
    if (!em->pipeline_ctrl) {
        em->pipeline_ctrl = pipeline_controller_create();
        if (!em->pipeline_ctrl) {
            return ECHO_ERR_MEMORY;
        }
    }

    /* Start the pipeline — validates language pair and creates all resources */
    int rc = pipeline_controller_start(em->pipeline_ctrl, src_lang, tgt_lang);
    if (rc != ECHO_OK) {
        return rc;
    }

    /* Transition to Running */
    em->state = ENGINE_RUNNING;
    em->session_active = true;

    return ECHO_OK;
}

int engine_manager_stop_pipeline(EngineManager* em) {
    if (!em) return ECHO_ERR_NOT_INITIALIZED;

    std::lock_guard<std::mutex> lock(em->mutex);

    /* No-op if no session is active (state is Ready or Uninitialized) */
    if (!em->session_active) {
        return ECHO_OK;
    }

    /* Transition to Stopping */
    em->state = ENGINE_STOPPING;

    /* Gracefully stop the pipeline via PipelineController.
     * This processes locked segments, discards unlocked audio, and
     * releases all pipeline resources within 2 seconds. */
    if (em->pipeline_ctrl) {
        pipeline_controller_stop(em->pipeline_ctrl);
    }

    /* Transition back to Ready, session ended */
    em->state = ENGINE_READY;
    em->session_active = false;

    return ECHO_OK;
}

void engine_manager_destroy(EngineManager* em) {
    if (!em) return;

    /* Stop any active session first */
    engine_manager_stop_pipeline(em);

    /* Destroy model loader and unload all models */
    {
        std::lock_guard<std::mutex> lock(em->mutex);
        if (em->model_loader) {
            model_loader_destroy(em->model_loader);
            em->model_loader = nullptr;
        }
        if (em->pipeline_ctrl) {
            pipeline_controller_destroy(em->pipeline_ctrl);
            em->pipeline_ctrl = nullptr;
        }
        em->state = ENGINE_UNINITIALIZED;
    }

    /* Explicitly destroy the mutex before freeing */
    em->mutex.~mutex();

    free(em);
}

EngineState engine_manager_get_state(const EngineManager* em) {
    if (!em) return ENGINE_UNINITIALIZED;
    /* State reads are safe without lock for query purposes;
     * all mutations are serialized under the mutex. */
    return em->state;
}
