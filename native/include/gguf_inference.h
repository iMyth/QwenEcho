/**
 * @file gguf_inference.h
 * @brief Thin C wrapper around llama.cpp for GGUF model inference.
 *
 * Provides a simplified API for loading GGUF models and running
 * text-to-text generation (tokenize → decode → sample → detokenize).
 */

#ifndef GGUF_INFERENCE_H
#define GGUF_INFERENCE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque inference context. */
typedef struct GgufContext GgufContext;

/**
 * Callback invoked for each generated token during streaming inference.
 *
 * @param token      UTF-8 text of the decoded token (NOT null-terminated).
 * @param token_len  Length of token in bytes.
 * @param user_data  Opaque pointer passed through from the caller.
 * @return 0 to continue generation, non-zero to stop early.
 */
typedef int (*gguf_token_callback)(const char* token, int token_len,
                                    void* user_data);

/**
 * Initialize the llama.cpp backend. Call once at program start.
 */
void gguf_inference_backend_init(void);

/**
 * Free the llama.cpp backend. Call once at program end.
 */
void gguf_inference_backend_free(void);

/**
 * Create an inference context by loading a GGUF model from a file path.
 *
 * @param model_path  Path to the GGUF model file.
 * @param n_ctx       Context window size (0 = use model default).
 * @param n_threads   Number of CPU threads (0 = auto-detect).
 * @return Pointer to context, or NULL on failure.
 */
GgufContext* gguf_inference_create(const char* model_path,
                                    int n_ctx, int n_threads);

/**
 * Destroy an inference context and free all resources.
 */
void gguf_inference_destroy(GgufContext* ctx);

/**
 * Run text generation and return the full output as a string.
 *
 * @param ctx        Inference context with loaded model.
 * @param prompt     Input prompt text (null-terminated).
 * @param output     Buffer to receive generated text (null-terminated).
 * @param output_cap Size of output buffer in bytes.
 * @param max_tokens Maximum number of tokens to generate.
 * @return Number of bytes written to output (excluding null), or negative on error.
 */
int gguf_inference_generate(GgufContext* ctx, const char* prompt,
                             char* output, size_t output_cap,
                             int max_tokens);

/**
 * Run streaming text generation with per-token callback.
 *
 * @param ctx        Inference context with loaded model.
 * @param prompt     Input prompt text (null-terminated).
 * @param callback   Called for each generated token.
 * @param user_data  Passed through to callback.
 * @param max_tokens Maximum number of tokens to generate.
 * @return 0 on success, negative on error.
 */
int gguf_inference_generate_streaming(GgufContext* ctx, const char* prompt,
                                       gguf_token_callback callback,
                                       void* user_data, int max_tokens);

/**
 * Reset the KV cache (conversation history) for a context.
 * Call between independent inference runs on the same model.
 */
void gguf_inference_reset(GgufContext* ctx);

#ifdef __cplusplus
}
#endif

#endif /* GGUF_INFERENCE_H */
