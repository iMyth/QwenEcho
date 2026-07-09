/**
 * @file tts_stage.h
 * @brief TTS Synthesis Stage — Public Interface.
 *
 * Dequeues translated text from the LLM→TTS bounded queue, performs
 * Qwen3-TTS-Streaming inference via the HAL accelerator (NPU-first with
 * CPU fallback), and outputs PCM audio at 24kHz, 16-bit, mono in
 * streaming chunks to the platform speaker via HAL audio output.
 *
 * SLA: ≤100ms TTFA (Time To First Audio) from dequeue to first PCM chunk.
 *
 * Behavior:
 *   - Discards whitespace-only or punctuation-only text segments (no TTS_STARTED)
 *   - Sends MSG_TTS_STARTED before synthesis begins
 *   - Sends MSG_TTS_COMPLETE when synthesis finishes
 *   - Reports MSG_LATENCY_WARNING("TTS", actual_ms, 100) on SLA violations
 *   - On failure: logs error, skips segment, continues to next
 *   - Operates on its own thread (concurrent with ASR/LLM)
 *
 * Validates: Requirements 7.1, 7.2, 7.3, 7.4, 7.5, 7.6, 8.4
 */

#ifndef TTS_STAGE_H
#define TTS_STAGE_H

#include "echo_types.h"
#include "hal_accelerator.h"

#ifdef __cplusplus
#include "bounded_spsc_queue.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque TTS stage handle.
 */
typedef struct TtsStage TtsStage;

#ifdef __cplusplus
}

/**
 * Create a new TTS synthesis stage instance.
 *
 * The TTS stage owns a worker thread that polls the input queue for
 * translated text and synthesizes speech asynchronously. Inference is
 * performed via the HAL accelerator (NPU-first).
 *
 * Audio output is PCM 24kHz, 16-bit, mono delivered in streaming chunks.
 *
 * @param accelerator   HAL accelerator context for NPU/GPU inference (may be NULL for stub mode)
 * @param input_queue   Bounded SPSC queue for translated LLM→TTS text elements (producer: LLM)
 * @return Pointer to created TTS stage, or nullptr on failure
 */
TtsStage* tts_stage_create(AcceleratorContext* accelerator,
                           BoundedSPSCQueue<LlmToTtsElement>* input_queue,
                           const char* model_path);

extern "C" {
#endif

/**
 * Destroy the TTS stage and release all resources.
 *
 * Stops the worker thread and waits for it to finish processing
 * any in-flight synthesis before returning.
 *
 * @param stage  TTS stage instance (NULL is safely ignored)
 */
void tts_stage_destroy(TtsStage* stage);

#ifdef __cplusplus
}
#endif

#endif /* TTS_STAGE_H */
