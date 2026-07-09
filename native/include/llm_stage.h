/**
 * @file llm_stage.h
 * @brief LLM Translation Stage — Public Interface.
 *
 * Dequeues confirmed ASR text from the ASR→LLM bounded queue, runs
 * Qwen3-4B-Instruct inference via the HAL accelerator (NPU-first with
 * CPU fallback), streams translation tokens to the UI via Native Port,
 * and enqueues translated text into the LLM→TTS bounded queue.
 *
 * SLA: ≤450ms first-token latency from dequeue to first output token.
 * Throughput: ≥35 tokens/second.
 *
 * Context window management:
 *   - Normal mode (0): 512-token window
 *   - Throttle mode (1): 256-token window
 *   - Sliding context: last 3 confirmed translations prepended
 *   - Truncation: oldest context entries removed first when over limit
 *   - Mid-translation mode change: finish current with original window
 *
 * Cascade truncation: emit partial results at punctuation boundaries
 * for downstream TTS consumption.
 *
 * Validates: Requirements 6.1, 6.2, 6.3, 6.4, 6.5, 6.6, 6.7, 6.8, 6.9, 8.2
 */

#ifndef LLM_STAGE_H
#define LLM_STAGE_H

#include "echo_types.h"
#include "hal_accelerator.h"

#ifdef __cplusplus
#include "bounded_spsc_queue.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque LLM stage handle.
 */
typedef struct LlmStage LlmStage;

#ifdef __cplusplus
}

/**
 * Create a new LLM translation stage instance.
 *
 * The LLM stage owns a worker thread that polls the input queue for
 * confirmed ASR text and translates it asynchronously. Inference is
 * performed via the HAL accelerator (NPU-first).
 *
 * @param accelerator   HAL accelerator context for NPU/GPU inference (may be NULL for stub mode)
 * @param input_queue   Bounded SPSC queue for confirmed ASR→LLM text elements (producer: ASR)
 * @param output_queue  Bounded SPSC queue for translated LLM→TTS text elements (consumer: TTS)
 * @return Pointer to created LLM stage, or nullptr on failure
 */
LlmStage* llm_stage_create(AcceleratorContext* accelerator,
                            BoundedSPSCQueue<AsrToLlmElement>* input_queue,
                            BoundedSPSCQueue<LlmToTtsElement>* output_queue,
                            const char* model_path);

extern "C" {
#endif

/**
 * Set the thermal mode for the LLM stage.
 *
 * Mode changes take effect on the next translation segment. If a translation
 * is currently in progress, it completes with its original context window size.
 *
 * @param stage         LLM stage instance
 * @param throttle_mode 0 = Normal (512-token window), 1 = Throttle (256-token window)
 */
void llm_stage_set_thermal_mode(LlmStage* stage, int throttle_mode);

/**
 * Destroy the LLM stage and release all resources.
 *
 * Stops the worker thread and waits for it to finish processing
 * any in-flight translations before returning.
 *
 * @param stage  LLM stage instance (NULL is safely ignored)
 */
void llm_stage_destroy(LlmStage* stage);

#ifdef __cplusplus
}
#endif

#endif /* LLM_STAGE_H */
