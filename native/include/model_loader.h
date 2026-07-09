/**
 * @file model_loader.h
 * @brief Model Loader interface for GGUF model validation, memory mapping,
 *        and inference context management.
 *
 * Handles:
 *   - GGUF header magic byte validation (0x46475547)
 *   - INT4 quantization format verification
 *   - Memory-mapped file access via mmap for OS page cache leverage
 *   - Independent inference context instantiation for ASR, LLM, TTS
 *   - Per-model memory consumption reporting
 *   - Categorized error reporting (missing, permission denied, invalid format)
 */

#ifndef MODEL_LOADER_H
#define MODEL_LOADER_H

#include <stddef.h>
#include "echo_types.h"
#include "hal_accelerator.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * GGUF file magic bytes: "GGUF" = {0x47,0x47,0x55,0x46}.
 * Read as a little-endian uint32 this is 0x46554747.
 */
#define GGUF_MAGIC 0x46554747u

/**
 * GGUF quantization type codes (ggml type system).
 * We accept K-quants and legacy 4-bit quants for mobile deployment.
 */
typedef enum {
    GGUF_QUANT_F32    = 0,
    GGUF_QUANT_F16    = 1,
    GGUF_QUANT_Q4_0   = 2,   /**< INT4 quantization (block-wise, no offset) */
    GGUF_QUANT_Q4_1   = 3,   /**< INT4 quantization (block-wise, with offset) */
    GGUF_QUANT_Q5_0   = 6,
    GGUF_QUANT_Q5_1   = 7,
    GGUF_QUANT_Q8_0   = 8,
    GGUF_QUANT_Q8_1   = 9,
    GGUF_QUANT_Q2_K   = 10,
    GGUF_QUANT_Q3_K   = 11,  /**< ~3.4 bpw K-quant */
    GGUF_QUANT_Q4_K   = 12,  /**< ~4.5 bpw K-quant */
    GGUF_QUANT_Q5_K   = 13,  /**< ~5.5 bpw K-quant */
    GGUF_QUANT_Q6_K   = 14,  /**< ~6.6 bpw K-quant */
    GGUF_QUANT_IQ4_NL = 27,  /**< ~4.5 bpw importance-matrix quant */
    GGUF_QUANT_IQ4_XS = 28,  /**< ~4.25 bpw importance-matrix quant */
} GgufQuantType;

/**
 * GGUF file header structure (v3).
 * The first fields of a valid GGUF file.
 */
typedef struct {
    uint32_t magic;           /**< Must be GGUF_MAGIC (0x46475547) */
    uint32_t version;         /**< GGUF format version (2 or 3) */
    uint64_t tensor_count;    /**< Number of tensors in the file */
    uint64_t metadata_kv_count; /**< Number of key-value pairs in metadata */
} GgufHeader;

/**
 * Opaque model loader context.
 */
typedef struct ModelLoader ModelLoader;

/**
 * Per-model information and status.
 */
typedef struct {
    ModelType type;           /**< Model type (ASR, LLM, TTS) */
    size_t   file_size;      /**< Size of the model file on disk (bytes) */
    size_t   memory_usage;   /**< Memory mapped + inference context usage (bytes) */
    int      loaded;         /**< 1 if model is loaded and ready, 0 otherwise */
} ModelInfo;

/**
 * Create a new model loader instance.
 *
 * @return Pointer to new ModelLoader, or NULL on allocation failure.
 */
ModelLoader* model_loader_create(void);

/**
 * Load and validate a GGUF model file.
 *
 * Performs the following steps:
 *   1. Check file exists → ECHO_ERR_MODEL_MISSING if not
 *   2. Check file permissions → ECHO_ERR_MODEL_PERMISSION if unreadable
 *   3. Read header magic → ECHO_ERR_MODEL_INVALID if != 0x46475547
 *   4. Validate quantization → ECHO_ERR_MODEL_INVALID if not an accepted
 *      quant (Q4_0/Q4_1, K-quants Q3_K–Q6_K, or IQ4 variants).
 *      Uses general.file_type metadata when available, otherwise scans
 *      for a representative weight tensor (token_embd / blk.0.*).
 *   5. mmap the file for efficient memory access
 *   6. Create inference context for the model type
 *
 * @param loader  Model loader instance.
 * @param path    Path to the GGUF model file.
 * @param type    Model type (MODEL_TYPE_ASR, MODEL_TYPE_LLM, MODEL_TYPE_TTS).
 * @return ECHO_OK on success, negative EchoErrorCode on failure.
 */
int model_loader_load(ModelLoader* loader, const char* path, ModelType type);

/**
 * Get information about a loaded model.
 *
 * @param loader  Model loader instance.
 * @param type    Model type to query.
 * @return ModelInfo struct (loaded=0 if model not loaded).
 */
ModelInfo model_loader_get_info(const ModelLoader* loader, ModelType type);

/**
 * Get the inference context for a loaded model.
 *
 * @param loader  Model loader instance.
 * @param type    Model type to query.
 * @return Opaque pointer to inference context, or NULL if model not loaded.
 */
void* model_loader_get_context(ModelLoader* loader, ModelType type);

/**
 * Get the file path for a loaded model.
 *
 * @param loader  Model loader instance.
 * @param type    Model type to query.
 * @return File path string (valid until unload/destroy), or NULL if not loaded.
 */
const char* model_loader_get_path(const ModelLoader* loader, ModelType type);

/**
 * Unload a specific model and release its resources.
 *
 * Unmaps the memory-mapped file and destroys the inference context.
 *
 * @param loader  Model loader instance.
 * @param type    Model type to unload.
 */
void model_loader_unload(ModelLoader* loader, ModelType type);

/**
 * Destroy the model loader and all loaded models.
 *
 * @param loader  Model loader instance. NULL is safely ignored.
 */
void model_loader_destroy(ModelLoader* loader);

#ifdef __cplusplus
}
#endif

#endif /* MODEL_LOADER_H */
