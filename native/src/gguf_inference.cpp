/**
 * @file gguf_inference.cpp
 * @brief Thin C++ wrapper around llama.cpp for GGUF model inference.
 *
 * Implements the gguf_inference.h API using llama.cpp's model loading,
 * tokenization, decoding, sampling, and detokenization functions.
 */

#include "gguf_inference.h"

#include "llama.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <os/log.h>
#define ECHO_LOG(fmt, ...) do { os_log(OS_LOG_DEFAULT, fmt, ##__VA_ARGS__); fprintf(stderr, fmt "\n", ##__VA_ARGS__); } while(0)
#else
#define ECHO_LOG(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#endif

/* -------------------------------------------------------------------------- */
/* Internal context                                                           */
/* -------------------------------------------------------------------------- */

struct GgufContext {
    llama_model*   model;
    llama_context* ctx;
    llama_sampler* sampler;

    int n_threads;
    int n_ctx;  /* actual context window size */
};

/* -------------------------------------------------------------------------- */
/* Backend lifecycle                                                          */
/* -------------------------------------------------------------------------- */

void gguf_inference_backend_init(void) {
    llama_backend_init();
    ECHO_LOG("[GgufInference] Backend initialized");
}

void gguf_inference_backend_free(void) {
    llama_backend_free();
    ECHO_LOG("[GgufInference] Backend freed");
}

/* -------------------------------------------------------------------------- */
/* Context lifecycle                                                          */
/* -------------------------------------------------------------------------- */

GgufContext* gguf_inference_create(const char* model_path,
                                    int n_ctx, int n_threads) {
    if (!model_path) {
        ECHO_LOG("[GgufInference] NULL model_path");
        return nullptr;
    }

    /* Verify file exists and is readable */
    FILE* f = fopen(model_path, "rb");
    if (!f) {
        ECHO_LOG("[GgufInference] Cannot open file: %{public}s (errno=%d)",
                 model_path, errno);
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fclose(f);
    ECHO_LOG("[GgufInference] File size: %ld bytes (%.1f MB): %{public}s",
             file_size, file_size / (1024.0 * 1024.0), model_path);

    /* --- Load model --- */
    llama_model_params mparams = llama_model_default_params();
    /* Use mmap for efficient memory usage on mobile */
    mparams.use_mmap = true;

    ECHO_LOG("[GgufInference] Calling llama_model_load_from_file...");
    llama_model* model = llama_model_load_from_file(model_path, mparams);
    if (!model) {
        ECHO_LOG("[GgufInference] llama_model_load_from_file FAILED: %{public}s", model_path);
        return nullptr;
    }
    ECHO_LOG("[GgufInference] Model loaded from file successfully");

    /* --- Create context --- */
    llama_context_params cparams = llama_context_default_params();

    if (n_ctx > 0) {
        cparams.n_ctx = static_cast<uint32_t>(n_ctx);
    } else {
        /* Use model's training context size, capped at 2048 for mobile */
        int32_t train_ctx = llama_model_n_ctx_train(model);
        cparams.n_ctx = static_cast<uint32_t>(
            (train_ctx > 2048) ? 2048 : train_ctx);
    }

    if (n_threads > 0) {
        cparams.n_threads = n_threads;
        cparams.n_threads_batch = n_threads;
    }

    /* Batch size for prompt processing */
    cparams.n_batch = 512;

    llama_context* ctx = llama_init_from_model(model, cparams);
    if (!ctx) {
        ECHO_LOG("[GgufInference] Failed to create context");
        llama_model_free(model);
        return nullptr;
    }

    /* --- Create sampler chain --- */
    llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    llama_sampler* sampler = llama_sampler_chain_init(sparams);

    /* Greedy sampling for deterministic output (good for translation/ASR) */
    llama_sampler_chain_add(sampler, llama_sampler_init_greedy());

    /* --- Build result --- */
    auto* gc = new (std::nothrow) GgufContext();
    if (!gc) {
        llama_sampler_free(sampler);
        llama_free(ctx);
        llama_model_free(model);
        return nullptr;
    }

    gc->model = model;
    gc->ctx = ctx;
    gc->sampler = sampler;
    gc->n_threads = (n_threads > 0) ? n_threads : 4;
    gc->n_ctx = static_cast<int>(llama_n_ctx(ctx));

    char desc[256] = {};
    llama_model_desc(model, desc, sizeof(desc));
    ECHO_LOG("[GgufInference] Model loaded: %{public}s, ctx=%d",
             desc, gc->n_ctx);

    return gc;
}

void gguf_inference_destroy(GgufContext* ctx) {
    if (!ctx) return;

    if (ctx->sampler) llama_sampler_free(ctx->sampler);
    if (ctx->ctx)     llama_free(ctx->ctx);
    if (ctx->model)   llama_model_free(ctx->model);

    delete ctx;
}

void gguf_inference_reset(GgufContext* ctx) {
    if (!ctx || !ctx->ctx) return;

    llama_memory_t mem = llama_get_memory(ctx->ctx);
    if (mem) {
        llama_memory_clear(mem, true);
    }
}

/* -------------------------------------------------------------------------- */
/* Inference helpers                                                          */
/* -------------------------------------------------------------------------- */

/**
 * Tokenize a prompt string into llama tokens.
 * Returns the token vector, or empty on failure.
 */
static std::vector<llama_token> tokenize_prompt(GgufContext* gc,
                                                  const char* prompt) {
    const llama_vocab* vocab = llama_model_get_vocab(gc->model);
    if (!vocab) return {};

    int32_t prompt_len = static_cast<int32_t>(strlen(prompt));

    /* Allocate enough space (worst case: 1 token per byte + specials) */
    std::vector<llama_token> tokens(prompt_len + 16);

    int32_t n_tokens = llama_tokenize(vocab, prompt, prompt_len,
                                       tokens.data(),
                                       static_cast<int32_t>(tokens.size()),
                                       true,  /* add_special (BOS) */
                                       false  /* parse_special */);
    if (n_tokens < 0) {
        /* Retry with larger buffer */
        tokens.resize(-n_tokens + 16);
        n_tokens = llama_tokenize(vocab, prompt, prompt_len,
                                   tokens.data(),
                                   static_cast<int32_t>(tokens.size()),
                                   true, false);
        if (n_tokens < 0) return {};
    }

    tokens.resize(static_cast<size_t>(n_tokens));
    return tokens;
}

/**
 * Detokenize a single token to a string piece.
 */
static std::string token_to_piece(GgufContext* gc, llama_token token) {
    const llama_vocab* vocab = llama_model_get_vocab(gc->model);
    if (!vocab) return "";

    char buf[256];
    int32_t n = llama_token_to_piece(vocab, token, buf, sizeof(buf), 0, true);
    if (n < 0) return "";
    return std::string(buf, static_cast<size_t>(n));
}

/**
 * Run the decode + sample loop.
 * Returns generated tokens.
 */
static std::vector<llama_token> run_generation(
    GgufContext* gc,
    const std::vector<llama_token>& prompt_tokens,
    int max_tokens,
    gguf_token_callback callback,
    void* user_data)
{
    const llama_vocab* vocab = llama_model_get_vocab(gc->model);
    std::vector<llama_token> generated;
    generated.reserve(static_cast<size_t>(max_tokens));

    /* Process the prompt batch */
    llama_batch prompt_batch = llama_batch_get_one(
        const_cast<llama_token*>(prompt_tokens.data()),
        static_cast<int32_t>(prompt_tokens.size()));

    int32_t ret = llama_decode(gc->ctx, prompt_batch);
    if (ret != 0) {
        ECHO_LOG("[GgufInference] Prompt decode failed: %d", ret);
        return generated;
    }

    /* Sample the first token from the last position */
    llama_token next_token = llama_sampler_sample(
        gc->sampler, gc->ctx, -1);

    /* Generation loop */
    for (int i = 0; i < max_tokens; ++i) {
        /* Check for end-of-generation */
        if (llama_vocab_is_eog(vocab, next_token)) {
            break;
        }

        generated.push_back(next_token);

        /* Detokenize for callback */
        if (callback) {
            std::string piece = token_to_piece(gc, next_token);
            if (!piece.empty()) {
                int stop = callback(piece.c_str(),
                                     static_cast<int>(piece.size()),
                                     user_data);
                if (stop != 0) break;
            }
        }

        /* Feed the sampled token back for next decode step */
        llama_batch step_batch = llama_batch_get_one(&next_token, 1);
        ret = llama_decode(gc->ctx, step_batch);
        if (ret != 0) {
            ECHO_LOG("[GgufInference] Decode step %d failed: %d", i, ret);
            break;
        }

        /* Sample next token */
        next_token = llama_sampler_sample(gc->sampler, gc->ctx, -1);
    }

    return generated;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

int gguf_inference_generate(GgufContext* ctx, const char* prompt,
                             char* output, size_t output_cap,
                             int max_tokens) {
    if (!ctx || !prompt || !output || output_cap == 0) return -1;
    if (max_tokens <= 0) max_tokens = 256;

    /* Reset KV cache for fresh generation */
    gguf_inference_reset(ctx);

    /* Tokenize prompt */
    std::vector<llama_token> prompt_tokens = tokenize_prompt(ctx, prompt);
    if (prompt_tokens.empty()) {
        ECHO_LOG("[GgufInference] Tokenization failed");
        output[0] = '\0';
        return -2;
    }

    ECHO_LOG("[GgufInference] Prompt: %zu tokens, generating max %d tokens",
             prompt_tokens.size(), max_tokens);

    /* Run generation (no streaming callback) */
    std::vector<llama_token> generated =
        run_generation(ctx, prompt_tokens, max_tokens, nullptr, nullptr);

    /* Detokenize all generated tokens into output buffer */
    if (generated.empty()) {
        output[0] = '\0';
        return 0;
    }

    const llama_vocab* vocab = llama_model_get_vocab(ctx->model);
    int32_t n_written = llama_detokenize(
        vocab, generated.data(),
        static_cast<int32_t>(generated.size()),
        output, static_cast<int32_t>(output_cap - 1),
        false, /* remove_special */
        true   /* unparse_special */);

    if (n_written < 0) {
        /* Output buffer too small — truncate */
        n_written = static_cast<int32_t>(output_cap - 1);
    }

    output[n_written] = '\0';

    ECHO_LOG("[GgufInference] Generated %zu tokens, %d bytes",
             generated.size(), n_written);

    return n_written;
}

int gguf_inference_generate_streaming(GgufContext* ctx, const char* prompt,
                                       gguf_token_callback callback,
                                       void* user_data, int max_tokens) {
    if (!ctx || !prompt || !callback) return -1;
    if (max_tokens <= 0) max_tokens = 256;

    /* Reset KV cache for fresh generation */
    gguf_inference_reset(ctx);

    /* Tokenize prompt */
    std::vector<llama_token> prompt_tokens = tokenize_prompt(ctx, prompt);
    if (prompt_tokens.empty()) {
        ECHO_LOG("[GgufInference] Streaming tokenization failed");
        return -2;
    }

    ECHO_LOG("[GgufInference] Streaming: %zu prompt tokens, max %d",
             prompt_tokens.size(), max_tokens);

    /* Run generation with streaming callback */
    std::vector<llama_token> generated =
        run_generation(ctx, prompt_tokens, max_tokens, callback, user_data);

    ECHO_LOG("[GgufInference] Streaming done: %zu tokens generated",
             generated.size());

    return 0;
}
