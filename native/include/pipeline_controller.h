/**
 * @file pipeline_controller.h
 * @brief Pipeline Controller — orchestrates the full audio processing pipeline.
 *
 * Manages creation, startup, and graceful shutdown of all pipeline components:
 *   AudioRingBuffer, BoundedSPSCQueues, AudioCollector, SentenceSegmenter,
 *   AsrStage, LlmStage, TtsStage, ThermalMonitor, MemoryMonitor.
 *
 * Graceful stop semantics (StopEchoPipeline):
 *   1. Signal AudioCollector to stop (no new audio enters the ring buffer)
 *   2. Wait for SentenceSegmenter to flush any locked segments through ASR→LLM→TTS
 *   3. Destroy all pipeline stage threads
 *   4. Clear the ring buffer (discard unlocked audio)
 *   5. All cleanup completes within 2 seconds
 *
 * Language validation:
 *   - Validates source and target language codes against FunASR's 52-language support
 *   - Returns ECHO_ERR_UNSUPPORTED_LANG for unknown ISO 639-1 codes
 *
 * Validates: Requirements 2.1, 2.2, 2.5
 */

#ifndef PIPELINE_CONTROLLER_H
#define PIPELINE_CONTROLLER_H

#include "echo_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque Pipeline Controller handle.
 */
typedef struct PipelineController PipelineController;

/**
 * Create a new Pipeline Controller instance.
 *
 * The controller is created in a stopped state. Call pipeline_controller_start()
 * to create and launch all pipeline resources.
 *
 * @return Pointer to the new PipelineController, or NULL on allocation failure.
 */
PipelineController* pipeline_controller_create(void);

/**
 * Start the pipeline with the given language pair.
 *
 * Validates language codes against supported ISO 639-1 codes, then creates
 * all pipeline resources (ring buffer, queues, stages, monitors) and starts
 * all component threads.
 *
 * @param pc        Pipeline Controller instance.
 * @param src_lang  ISO 639-1 source language code (e.g. "zh", "en").
 * @param tgt_lang  ISO 639-1 target language code (e.g. "en", "ja").
 * @return ECHO_OK on success.
 *         ECHO_ERR_UNSUPPORTED_LANG if either language code is not supported.
 *         ECHO_ERR_SESSION_ACTIVE if the pipeline is already running.
 *         ECHO_ERR_MEMORY on allocation failure.
 */
int pipeline_controller_start(PipelineController* pc, const char* src_lang,
                              const char* tgt_lang);

/**
 * Gracefully stop the pipeline.
 *
 * Stop sequence:
 *   1. Stop AudioCollector (no new audio)
 *   2. Flush locked segments through ASR→LLM→TTS (with 2s deadline)
 *   3. Destroy all pipeline stages and threads
 *   4. Discard unlocked audio in ring buffer
 *   5. Release all pipeline resources
 *
 * If no pipeline is running, this is a no-op returning ECHO_OK.
 * All cleanup completes within 2 seconds.
 *
 * @param pc  Pipeline Controller instance.
 * @return ECHO_OK on success.
 */
int pipeline_controller_stop(PipelineController* pc);

/**
 * Query whether the pipeline is currently running.
 *
 * @param pc  Pipeline Controller instance (may be NULL).
 * @return true if the pipeline is active, false otherwise.
 */
bool pipeline_controller_is_running(const PipelineController* pc);

/**
 * Destroy the Pipeline Controller and release all resources.
 *
 * If the pipeline is running, it will be stopped first.
 * NULL is safely ignored.
 *
 * @param pc  Pipeline Controller instance to destroy.
 */
void pipeline_controller_destroy(PipelineController* pc);

#ifdef __cplusplus
}
#endif

#endif /* PIPELINE_CONTROLLER_H */
