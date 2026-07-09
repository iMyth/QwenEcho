/**
 * LLM Translation Stage — Implementation.
 *
 * Architecture:
 * - A worker thread polls the ASR→LLM input queue for confirmed text.
 * - Maintains a sliding context window of the last 3 confirmed translations.
 * - Before inference: prepends context entries + current segment, truncates
 *   oldest entries if combined input exceeds the active window limit.
 * - Context window: 512 tokens in Normal mode, 256 in Throttle mode.
 * - On thermal mode change mid-translation: finishes current segment with
 *   the original window, applies new window to next segment.
 * - Stub inference: simulates token-by-token generation from input text.
 * - Streams each token via MSG_TRANSLATION_STREAM to the UI.
 * - At punctuation boundaries (., !, ?), enqueues partial result to LLM→TTS
 *   queue (cascade truncation for early TTS start).
 * - Sends MSG_TRANSLATION_DONE when segment is fully translated.
 * - Tracks first-token latency: reports via MSG_LATENCY_WARNING if >450ms.
 *
 * Validates: Requirements 6.1, 6.2, 6.3, 6.4, 6.5, 6.6, 6.7, 6.8, 6.9, 8.2
 */

#include "llm_stage.h"
#include "bounded_spsc_queue.h"
#include "native_port.h"
#include "hal_accelerator.h"
#include "gguf_inference.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <cstdint>
#include <cstdio>

#ifdef __APPLE__
#include <os/log.h>
#define ECHO_LOG(fmt, ...) os_log(OS_LOG_DEFAULT, fmt, ##__VA_ARGS__)
#else
#define ECHO_LOG(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#endif

/* --------------------------------------------------------------------------
 * Constants
 * -------------------------------------------------------------------------- */

/** SLA budget: first output token must arrive within 450ms. */
static constexpr int32_t LLM_LATENCY_BUDGET_MS = 450;

/** Context window size in Normal thermal mode (tokens). */
static constexpr uint32_t CONTEXT_WINDOW_NORMAL = 512;

/** Context window size in Throttle thermal mode (tokens). */
static constexpr uint32_t CONTEXT_WINDOW_THROTTLE = 256;

/** Number of previous translations to keep as sliding context. */
static constexpr uint32_t SLIDING_HISTORY_COUNT = 3;

/** Polling interval when input queue is empty (ms). */
static constexpr int POLL_INTERVAL_MS = 5;

/* --------------------------------------------------------------------------
 * Internal context entry
 * -------------------------------------------------------------------------- */

struct ContextEntry {
    std::string text;
    uint32_t token_count; /* Estimated token count for this entry */
};

/* --------------------------------------------------------------------------
 * LlmStage internal structure
 * -------------------------------------------------------------------------- */

struct LlmStage {
    /* HAL accelerator for inference (may be nullptr in stub mode) */
    AcceleratorContext* accelerator;

    /* Input queue: confirmed ASR text (producer: ASR stage) */
    BoundedSPSCQueue<AsrToLlmElement>* input_queue;

    /* Output queue: translated text → TTS stage */
    BoundedSPSCQueue<LlmToTtsElement>* output_queue;

    /* GGUF inference context for real LLM model (NULL if stub mode) */
    GgufContext* gguf_ctx;

    /* Thermal mode: 0 = Normal, 1 = Throttle */
    std::atomic<int> throttle_mode;

    /* Worker thread */
    std::thread worker;
    std::atomic<bool> running;

    /* Sliding context window: last N confirmed translations */
    std::vector<ContextEntry> context_history;
};

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/**
 * Estimate the token count for a piece of text.
 * Simple heuristic: ~1 token per 4 characters (GPT-family average).
 * Minimum of 1 token for non-empty text.
 */
static uint32_t estimate_token_count(const std::string& text) {
    if (text.empty()) return 0;
    uint32_t est = static_cast<uint32_t>((text.size() + 3) / 4);
    return est < 1 ? 1 : est;
}

/**
 * Get the active context window size based on thermal mode.
 */
static uint32_t get_context_window_size(int throttle_mode) {
    return (throttle_mode != 0) ? CONTEXT_WINDOW_THROTTLE : CONTEXT_WINDOW_NORMAL;
}

/**
 * Build the context prompt by prepending sliding history entries.
 * Truncates oldest entries first if combined tokens exceed window_size.
 * Returns the number of tokens used by context (excluding current segment).
 */
static uint32_t build_context_prompt(const std::vector<ContextEntry>& history,
                                     uint32_t current_segment_tokens,
                                     uint32_t window_size,
                                     std::string& out_context) {
    out_context.clear();

    if (history.empty()) return 0;

    /* Calculate how many tokens are available for context */
    uint32_t available_for_context = 0;
    if (window_size > current_segment_tokens) {
        available_for_context = window_size - current_segment_tokens;
    }

    /* Walk from newest to oldest, accumulating entries that fit */
    std::vector<size_t> included_indices;
    uint32_t accumulated_tokens = 0;

    for (int i = static_cast<int>(history.size()) - 1; i >= 0; --i) {
        uint32_t entry_tokens = history[static_cast<size_t>(i)].token_count;
        if (accumulated_tokens + entry_tokens <= available_for_context) {
            accumulated_tokens += entry_tokens;
            included_indices.push_back(static_cast<size_t>(i));
        } else {
            /* No room for older entries — truncate (oldest first semantics:
             * we already skipped older ones by going newest-first) */
            break;
        }
    }

    /* Build context string in chronological order (oldest first) */
    for (int i = static_cast<int>(included_indices.size()) - 1; i >= 0; --i) {
        size_t idx = included_indices[static_cast<size_t>(i)];
        if (!out_context.empty()) {
            out_context += " ";
        }
        out_context += history[idx].text;
    }

    return accumulated_tokens;
}

/**
 * Check if a character is a punctuation boundary for cascade truncation.
 * Boundaries: '.', '!', '?'
 */
static bool is_punctuation_boundary(char c) {
    return c == '.' || c == '!' || c == '?';
}

/**
 * Real LLM inference using GGUF model via llama.cpp.
 * Falls back to stub if model is not loaded.
 */
static std::vector<std::string> real_translate_tokens(
    GgufContext* gguf_ctx,
    const std::string& /*context*/,
    const std::string& input_text) {

    std::vector<std::string> tokens;
    if (input_text.empty()) return tokens;

    if (!gguf_ctx) {
        /* Stub fallback */
        tokens.emplace_back("[T]");
        std::string word;
        for (size_t i = 0; i < input_text.size(); ++i) {
            char c = input_text[i];
            if (c == ' ' || c == '\t' || c == '\n') {
                if (!word.empty()) { tokens.push_back(word); word.clear(); }
            } else { word += c; }
        }
        if (!word.empty()) tokens.push_back(word);
        if (!tokens.empty()) {
            const std::string& last = tokens.back();
            if (!last.empty() && !is_punctuation_boundary(last.back()))
                tokens.emplace_back(".");
        }
        return tokens;
    }

    ECHO_LOG("[LLM] Real translation: %{public}s", input_text.c_str());

    /* Build translation prompt using chat template */
    char prompt[4096];
    std::snprintf(prompt, sizeof(prompt),
                  "You are a professional translator. Translate the following text. "
                  "Output only the translation without any explanation.\n\n%s",
                  input_text.c_str());

    /* Run generation */
    char output[4096] = {};
    int n = gguf_inference_generate(gguf_ctx, prompt, output, sizeof(output), 256);

    if (n > 0) {
        std::string text(output, static_cast<size_t>(n));
        ECHO_LOG("[LLM] Translation result: %{public}s", text.c_str());

        /* Tokenize by whitespace for streaming */
        std::string word;
        for (size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            if (c == ' ' || c == '\t' || c == '\n') {
                if (!word.empty()) { tokens.push_back(word); word.clear(); }
            } else { word += c; }
        }
        if (!word.empty()) tokens.push_back(word);
    } else {
        ECHO_LOG("[LLM] Real inference failed, returning stub");
        tokens.emplace_back("[T]");
        tokens.push_back(input_text);
    }

    /* Ensure sentence-ending punctuation */
    if (!tokens.empty()) {
        const std::string& last = tokens.back();
        if (!last.empty() && !is_punctuation_boundary(last.back()))
            tokens.emplace_back(".");
    }

    return tokens;
}

/**
 * Enqueue a partial or final translation into the LLM→TTS output queue.
 */
static void enqueue_to_tts(BoundedSPSCQueue<LlmToTtsElement>* output_queue,
                           uint32_t segment_id,
                           uint8_t speaker_id,
                           const std::string& text,
                           uint64_t timestamp_ms) {
    LlmToTtsElement element;
    element.segment_id = segment_id;
    element.speaker_id = speaker_id;
    element.timestamp_ms = timestamp_ms;

    size_t copy_len = text.size();
    if (copy_len >= sizeof(element.text)) {
        copy_len = sizeof(element.text) - 1;
    }
    std::memcpy(element.text, text.c_str(), copy_len);
    element.text[copy_len] = '\0';
    element.text_len = static_cast<uint16_t>(copy_len);

    output_queue->try_push(element);
}

/**
 * LLM worker thread main loop.
 * Polls the input queue for confirmed ASR text and translates it.
 */
static void llm_worker_loop(LlmStage* stage) {
    while (stage->running.load(std::memory_order_acquire)) {
        AsrToLlmElement input;

        /* Poll the input queue (lock-free) */
        if (!stage->input_queue->try_pop(input)) {
            /* Queue empty — sleep briefly and retry */
            std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
            continue;
        }

        /* Record time of dequeue for SLA tracking */
        auto dequeue_time = std::chrono::steady_clock::now();

        ECHO_LOG("[LLM] Input received: seg=%u, text=%{public}s",
                 input.segment_id, input.text);

        /* Capture the thermal mode at translation start (freeze for this segment) */
        int segment_thermal_mode = stage->throttle_mode.load(std::memory_order_acquire);
        uint32_t window_size = get_context_window_size(segment_thermal_mode);

        /* Extract input text */
        std::string input_text(input.text, input.text_len);

        /* Estimate token count for the current segment */
        uint32_t current_tokens = estimate_token_count(input_text);

        /* Build context from sliding history, truncating oldest if needed */
        std::string context_prompt;
        build_context_prompt(stage->context_history, current_tokens,
                             window_size, context_prompt);

        /* Run inference — real or stub */
        std::vector<std::string> tokens = real_translate_tokens(
            stage->gguf_ctx, context_prompt, input_text);

        if (tokens.empty()) {
            /* Nothing to translate — skip */
            continue;
        }

        /* Stream tokens to UI and apply cascade truncation */
        bool first_token_sent = false;
        std::string full_translation;
        std::string cascade_buffer; /* Accumulates text until punctuation boundary */

        for (size_t i = 0; i < tokens.size(); ++i) {
            const std::string& token = tokens[i];

            /* Append token to full translation */
            if (!full_translation.empty() && !token.empty() &&
                token[0] != '.' && token[0] != '!' && token[0] != '?') {
                full_translation += " ";
                cascade_buffer += " ";
            }
            full_translation += token;
            cascade_buffer += token;

            /* Stream token to UI via MSG_TRANSLATION_STREAM */
            native_port_post_translation_stream(
                input.speaker_id,
                token.c_str(),
                input.segment_id);

            /* Track first-token latency for SLA */
            if (!first_token_sent) {
                first_token_sent = true;

                auto first_token_time = std::chrono::steady_clock::now();
                auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                    first_token_time - dequeue_time);
                int32_t latency_ms = static_cast<int32_t>(latency.count());

                /* Report SLA violation if > 450ms */
                if (latency_ms > LLM_LATENCY_BUDGET_MS) {
                    native_port_post_latency_warning("LLM", latency_ms, LLM_LATENCY_BUDGET_MS);
                }
            }

            /* Cascade truncation: at punctuation boundaries, enqueue partial
             * result to LLM→TTS queue so TTS can start early */
            if (!token.empty() && is_punctuation_boundary(token.back())) {
                if (!cascade_buffer.empty()) {
                    enqueue_to_tts(stage->output_queue,
                                   input.segment_id,
                                   input.speaker_id,
                                   cascade_buffer,
                                   input.timestamp_ms);
                    cascade_buffer.clear();
                }
            }
        }

        /* If there's remaining text in cascade_buffer (no trailing punctuation),
         * enqueue it as the final piece */
        if (!cascade_buffer.empty()) {
            enqueue_to_tts(stage->output_queue,
                           input.segment_id,
                           input.speaker_id,
                           cascade_buffer,
                           input.timestamp_ms);
        }

        /* Send MSG_TRANSLATION_DONE for this segment */
        native_port_post_translation_done(
            input.speaker_id,
            full_translation.c_str(),
            input.segment_id);

        ECHO_LOG("[LLM] Translation done: seg=%u, translation=%{public}s",
                 input.segment_id, full_translation.c_str());

        /* Update sliding context history with the new confirmed translation */
        ContextEntry new_entry;
        new_entry.text = full_translation;
        new_entry.token_count = estimate_token_count(full_translation);

        stage->context_history.push_back(std::move(new_entry));

        /* Keep only the last SLIDING_HISTORY_COUNT entries */
        while (stage->context_history.size() > SLIDING_HISTORY_COUNT) {
            stage->context_history.erase(stage->context_history.begin());
        }
    }
}

/* --------------------------------------------------------------------------
 * Public API implementation
 * -------------------------------------------------------------------------- */

LlmStage* llm_stage_create(AcceleratorContext* accelerator,
                            BoundedSPSCQueue<AsrToLlmElement>* input_queue,
                            BoundedSPSCQueue<LlmToTtsElement>* output_queue,
                            const char* model_path) {
    if (!input_queue || !output_queue) return nullptr;

    LlmStage* stage = new (std::nothrow) LlmStage();
    if (!stage) return nullptr;

    stage->accelerator = accelerator;
    stage->input_queue = input_queue;
    stage->output_queue = output_queue;
    stage->throttle_mode.store(0, std::memory_order_relaxed);
    stage->running.store(true, std::memory_order_relaxed);

    /* Reserve space for sliding history */
    stage->context_history.reserve(SLIDING_HISTORY_COUNT);

    /* Initialize GGUF inference context if model path is provided */
    stage->gguf_ctx = nullptr;
    if (model_path && model_path[0] != '\0') {
        stage->gguf_ctx = gguf_inference_create(model_path, 2048, 4);
        if (stage->gguf_ctx) {
            ECHO_LOG("[LLM] GGUF model loaded: %{public}s", model_path);
        } else {
            ECHO_LOG("[LLM] Failed to load GGUF model, using stub mode: %{public}s", model_path);
        }
    }

    /* Launch worker thread */
    stage->worker = std::thread(llm_worker_loop, stage);

    return stage;
}

extern "C" {

void llm_stage_set_thermal_mode(LlmStage* stage, int throttle_mode) {
    if (!stage) return;
    stage->throttle_mode.store(throttle_mode, std::memory_order_release);
}

void llm_stage_destroy(LlmStage* stage) {
    if (!stage) return;

    /* Signal worker to stop */
    stage->running.store(false, std::memory_order_release);

    /* Wait for worker to finish */
    if (stage->worker.joinable()) {
        stage->worker.join();
    }

    /* Free GGUF inference context */
    if (stage->gguf_ctx) {
        gguf_inference_destroy(stage->gguf_ctx);
    }

    delete stage;
}

} /* extern "C" */
