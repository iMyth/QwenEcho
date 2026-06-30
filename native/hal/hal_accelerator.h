/**
 * @file hal_accelerator.h
 * @brief Hardware Accelerator HAL interface.
 *
 * Abstracts NPU/GPU inference acceleration across platforms:
 *   - Android: NNAPI 1.3+ / Vulkan Compute
 *   - iOS: CoreML 5+ / Metal Performance Shaders
 */

#ifndef HAL_ACCELERATOR_H
#define HAL_ACCELERATOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Model types supported by the accelerator.
 */
typedef enum {
    MODEL_TYPE_ASR = 0,  /**< FunASR-Nano speech recognition model */
    MODEL_TYPE_LLM = 1,  /**< Qwen3-4B-Instruct translation model */
    MODEL_TYPE_TTS = 2   /**< Qwen3-TTS-Streaming synthesis model */
} ModelType;

/**
 * Opaque accelerator context. Manages the lifecycle of loaded models
 * and inference sessions for a single model type.
 */
typedef struct AcceleratorContext AcceleratorContext;

/**
 * Create a new accelerator context.
 *
 * Initializes platform-specific acceleration backend (NNAPI/Vulkan on Android,
 * CoreML/Metal on iOS) with automatic CPU fallback.
 *
 * @return Pointer to new context, or NULL on failure.
 */
AcceleratorContext* hal_accelerator_create(void);

/**
 * Load a GGUF model into the accelerator context.
 *
 * @param ctx   Accelerator context (must not be NULL).
 * @param gguf_data  Pointer to the GGUF model data in memory.
 * @param size       Size of the model data in bytes.
 * @param type       Model type (ASR, LLM, or TTS).
 * @return 0 on success, negative error code on failure.
 */
int hal_accelerator_load_model(AcceleratorContext* ctx, const void* gguf_data,
                               size_t size, ModelType type);

/**
 * Run inference on the loaded model.
 *
 * @param ctx        Accelerator context with a loaded model.
 * @param input      Input tensor data (float array).
 * @param input_len  Number of elements in input.
 * @param output     Output buffer to receive inference results.
 * @param output_len On entry: capacity of output buffer. On exit: actual output length.
 * @return 0 on success, negative error code on failure.
 */
int hal_accelerator_infer(AcceleratorContext* ctx, const float* input,
                          size_t input_len, float* output, size_t* output_len);

/**
 * Destroy the accelerator context and release all resources.
 *
 * @param ctx  Context to destroy. NULL is safely ignored.
 */
void hal_accelerator_destroy(AcceleratorContext* ctx);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ACCELERATOR_H */
