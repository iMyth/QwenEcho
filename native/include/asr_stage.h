/**
 * @file asr_stage.h
 * @brief ASR Processing Stage — Public Interface.
 *
 * Receives locked audio segments from the Sentence Segmenter, runs
 * FunASR-Nano inference via the HAL accelerator (NPU-first with CPU fallback),
 * streams partial tokens to the UI via Native Port, and enqueues confirmed
 * text into the ASR→LLM bounded queue.
 *
 * SLA: ≤200ms first-character latency from segment receipt to first partial.
 *
 * Thermal modes:
 *   - Normal (0): Full 16kHz inference
 *   - Throttle (1): Resample 16kHz → 8kHz before inference
 *
 * Validates: Requirements 5.1, 5.2, 5.3, 5.4, 5.5, 5.6, 10.5
 */

#ifndef ASR_STAGE_H
#define ASR_STAGE_H

#include "echo_types.h"
#include "hal_accelerator.h"
#include "sentence_segmenter.h"

#ifdef __cplusplus
#include "bounded_spsc_queue.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque ASR stage handle.
 */
typedef struct AsrStage AsrStage;

#ifdef __cplusplus
}

/**
 * Create a new ASR stage instance.
 *
 * The ASR stage owns a worker thread that processes locked segments
 * asynchronously. Inference is performed via the HAL accelerator.
 *
 * @param accelerator   HAL accelerator context for NPU/GPU inference (may be NULL for stub mode)
 * @param output_queue  Bounded SPSC queue for confirmed ASR→LLM text elements
 * @return Pointer to created ASR stage, or nullptr on failure
 */
AsrStage* asr_stage_create(AcceleratorContext* accelerator,
                           BoundedSPSCQueue<AsrToLlmElement>* output_queue,
                           const char* model_path);

extern "C" {
#endif

/**
 * Process a locked audio segment through the ASR pipeline.
 *
 * This function enqueues the segment for asynchronous processing by the
 * ASR worker thread. It returns immediately (non-blocking).
 *
 * Processing flow:
 * 1. Optionally resample (16kHz→8kHz in Throttle mode)
 * 2. Run inference via HAL accelerator
 * 3. Stream partial tokens via MSG_ASR_PARTIAL
 * 4. Finalize confirmed text via MSG_ASR_CONFIRMED
 * 5. Enqueue confirmed text into output queue
 * 6. Report SLA violation if first-char latency > 200ms
 *
 * Noise-only/unintelligible segments are silently discarded (no confirmed
 * message, no queue push).
 *
 * @param stage    ASR stage instance
 * @param segment  Locked segment from the Sentence Segmenter (copied internally)
 * @return 0 on success, negative error code on failure
 */
int asr_stage_process_segment(AsrStage* stage, const LockedSegment* segment);

/**
 * Set the thermal mode for the ASR stage.
 *
 * @param stage         ASR stage instance
 * @param throttle_mode 0 = Normal (16kHz), 1 = Throttle (resample to 8kHz)
 */
void asr_stage_set_thermal_mode(AsrStage* stage, int throttle_mode);

/**
 * Destroy the ASR stage and release all resources.
 *
 * Stops the worker thread and waits for it to finish processing
 * any in-flight segments before returning.
 *
 * @param stage  ASR stage instance (NULL is safely ignored)
 */
void asr_stage_destroy(AsrStage* stage);

#ifdef __cplusplus
}
#endif

#endif /* ASR_STAGE_H */
