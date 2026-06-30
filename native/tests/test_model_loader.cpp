/**
 * @file test_model_loader.cpp
 * @brief Unit tests for the Model Loader component.
 *
 * Validates:
 * - GGUF magic byte validation
 * - INT4 quantization format checking
 * - mmap-based file loading
 * - Error categorization (missing, permission denied, invalid format)
 * - Per-model memory reporting
 * - Lifecycle (load, unload, destroy)
 */

#include "model_loader.h"
#include <rapidcheck.h>

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

/* ─── Helper: Create a minimal valid GGUF file ───────────────────────────── */

/**
 * Build a minimal valid GGUF binary with:
 *   - Correct magic (0x46475547)
 *   - Version 3
 *   - 1 tensor with the specified quant type
 *   - 0 metadata KV pairs
 *   - Some padding bytes to make it a plausible file
 */
static std::string make_gguf_binary(uint32_t quant_type) {
    std::string buf;
    buf.reserve(256);

    /* Header */
    GgufHeader hdr;
    hdr.magic = GGUF_MAGIC;
    hdr.version = 3;
    hdr.tensor_count = 1;
    hdr.metadata_kv_count = 0;
    buf.append(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    /* Tensor info: name_len(u64) + name + n_dims(u32) + dims + type(u32) + offset(u64) */
    const char* tensor_name = "weight.0";
    uint64_t name_len = strlen(tensor_name);
    buf.append(reinterpret_cast<const char*>(&name_len), 8);
    buf.append(tensor_name, name_len);

    uint32_t n_dims = 2;
    buf.append(reinterpret_cast<const char*>(&n_dims), 4);

    uint64_t dim0 = 128, dim1 = 64;
    buf.append(reinterpret_cast<const char*>(&dim0), 8);
    buf.append(reinterpret_cast<const char*>(&dim1), 8);

    buf.append(reinterpret_cast<const char*>(&quant_type), 4);

    uint64_t tensor_offset = 0;
    buf.append(reinterpret_cast<const char*>(&tensor_offset), 8);

    /* Add some padding to simulate tensor data */
    std::string padding(1024, '\0');
    buf.append(padding);

    return buf;
}

/**
 * Write binary data to a temporary file and return its path.
 */
static std::string write_temp_file(const std::string& data, const char* suffix = ".gguf") {
    char tmpl[] = "/tmp/qwen_echo_test_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) return "";

    std::string path = tmpl;
    path += suffix;
    close(fd);
    unlink(tmpl); /* Remove the mkstemp file, we'll write with the suffix */

    /* Write to path with suffix */
    std::string final_path(tmpl);
    std::ofstream ofs(final_path, std::ios::binary);
    ofs.write(data.data(), data.size());
    ofs.close();

    return final_path;
}

/**
 * Create a valid INT4 GGUF test file and return its path.
 */
static std::string create_valid_gguf() {
    std::string data = make_gguf_binary(GGUF_QUANT_Q4_0);
    return write_temp_file(data);
}

/**
 * Create a GGUF file with wrong magic bytes.
 */
static std::string create_bad_magic_gguf() {
    std::string data = make_gguf_binary(GGUF_QUANT_Q4_0);
    /* Corrupt magic */
    uint32_t bad_magic = 0xDEADBEEF;
    memcpy(&data[0], &bad_magic, 4);
    return write_temp_file(data);
}

/**
 * Create a GGUF file with non-INT4 quantization (FP16).
 */
static std::string create_fp16_gguf() {
    std::string data = make_gguf_binary(GGUF_QUANT_F16);
    return write_temp_file(data);
}

/* ─── Helper to check pointer non-null without triggering RapidCheck Show<void*> ── */
#define RC_ASSERT_NOT_NULL(ptr) RC_ASSERT(static_cast<bool>(ptr))
#define RC_ASSERT_NULL(ptr) RC_ASSERT(!static_cast<bool>(ptr))

/* ─── Tests ──────────────────────────────────────────────────────────────── */

int main() {
    /* Test: model_loader_create and destroy */
    rc::check("model_loader_create returns non-null, destroy is safe", []() {
        ModelLoader* loader = model_loader_create();
        RC_ASSERT_NOT_NULL(loader);

        /* All slots should be unloaded initially */
        for (int i = 0; i < 3; i++) {
            ModelInfo info = model_loader_get_info(loader, static_cast<ModelType>(i));
            RC_ASSERT(info.loaded == 0);
            RC_ASSERT(static_cast<size_t>(info.file_size) == static_cast<size_t>(0));
            RC_ASSERT(static_cast<size_t>(info.memory_usage) == static_cast<size_t>(0));
        }

        model_loader_destroy(loader);
    });

    /* Test: destroy(NULL) is safe */
    rc::check("model_loader_destroy(NULL) does not crash", []() {
        model_loader_destroy(nullptr);
    });

    /* Test: load missing file returns ECHO_ERR_MODEL_MISSING */
    rc::check("load missing file returns ECHO_ERR_MODEL_MISSING", []() {
        ModelLoader* loader = model_loader_create();
        int result = model_loader_load(loader, "/nonexistent/path/model.gguf", MODEL_TYPE_ASR);
        RC_ASSERT(result == ECHO_ERR_MODEL_MISSING);
        model_loader_destroy(loader);
    });

    /* Test: load unreadable file returns ECHO_ERR_MODEL_PERMISSION */
    rc::check("load unreadable file returns ECHO_ERR_MODEL_PERMISSION", []() {
        /* Create a file then remove read permission */
        std::string path = create_valid_gguf();
        if (path.empty()) return; /* Skip if we can't create temp files */

        chmod(path.c_str(), 0000);
        ModelLoader* loader = model_loader_create();
        int result = model_loader_load(loader, path.c_str(), MODEL_TYPE_ASR);

        /* If running as root, permission check may not work - handle gracefully */
        if (getuid() != 0) {
            RC_ASSERT(result == ECHO_ERR_MODEL_PERMISSION);
        }

        model_loader_destroy(loader);
        chmod(path.c_str(), 0644); /* Restore for cleanup */
        unlink(path.c_str());
    });

    /* Test: load file with bad magic returns ECHO_ERR_MODEL_INVALID */
    rc::check("load file with bad magic returns ECHO_ERR_MODEL_INVALID", []() {
        std::string path = create_bad_magic_gguf();
        if (path.empty()) return;

        ModelLoader* loader = model_loader_create();
        int result = model_loader_load(loader, path.c_str(), MODEL_TYPE_LLM);
        RC_ASSERT(result == ECHO_ERR_MODEL_INVALID);

        model_loader_destroy(loader);
        unlink(path.c_str());
    });

    /* Test: load file with non-INT4 quant returns ECHO_ERR_MODEL_INVALID */
    rc::check("load file with FP16 quant returns ECHO_ERR_MODEL_INVALID", []() {
        std::string path = create_fp16_gguf();
        if (path.empty()) return;

        ModelLoader* loader = model_loader_create();
        int result = model_loader_load(loader, path.c_str(), MODEL_TYPE_TTS);
        RC_ASSERT(result == ECHO_ERR_MODEL_INVALID);

        model_loader_destroy(loader);
        unlink(path.c_str());
    });

    /* Test: successfully load a valid INT4 GGUF file */
    rc::check("load valid INT4 GGUF file succeeds", []() {
        std::string path = create_valid_gguf();
        if (path.empty()) return;

        ModelLoader* loader = model_loader_create();
        int result = model_loader_load(loader, path.c_str(), MODEL_TYPE_ASR);
        RC_ASSERT(result == ECHO_OK);

        /* Verify info reports loaded state */
        ModelInfo info = model_loader_get_info(loader, MODEL_TYPE_ASR);
        RC_ASSERT(info.loaded == 1);
        RC_ASSERT(info.file_size > static_cast<size_t>(0));
        RC_ASSERT(info.memory_usage > static_cast<size_t>(0));
        RC_ASSERT(info.memory_usage >= info.file_size); /* mapped + context */

        /* Verify context is available */
        RC_ASSERT_NOT_NULL(model_loader_get_context(loader, MODEL_TYPE_ASR));

        /* Other slots remain unloaded */
        ModelInfo llm_info = model_loader_get_info(loader, MODEL_TYPE_LLM);
        RC_ASSERT(llm_info.loaded == 0);

        model_loader_destroy(loader);
        unlink(path.c_str());
    });

    /* Test: load Q4_1 variant also succeeds */
    rc::check("load Q4_1 GGUF file succeeds", []() {
        std::string data = make_gguf_binary(GGUF_QUANT_Q4_1);
        std::string path = write_temp_file(data);
        if (path.empty()) return;

        ModelLoader* loader = model_loader_create();
        int result = model_loader_load(loader, path.c_str(), MODEL_TYPE_LLM);
        RC_ASSERT(result == ECHO_OK);

        model_loader_destroy(loader);
        unlink(path.c_str());
    });

    /* Test: load Q4_K variant also succeeds */
    rc::check("load Q4_K GGUF file succeeds", []() {
        std::string data = make_gguf_binary(GGUF_QUANT_Q4_K);
        std::string path = write_temp_file(data);
        if (path.empty()) return;

        ModelLoader* loader = model_loader_create();
        int result = model_loader_load(loader, path.c_str(), MODEL_TYPE_TTS);
        RC_ASSERT(result == ECHO_OK);

        model_loader_destroy(loader);
        unlink(path.c_str());
    });

    /* Test: unload frees resources */
    rc::check("unload frees model resources", []() {
        std::string path = create_valid_gguf();
        if (path.empty()) return;

        ModelLoader* loader = model_loader_create();
        model_loader_load(loader, path.c_str(), MODEL_TYPE_ASR);

        model_loader_unload(loader, MODEL_TYPE_ASR);

        ModelInfo info = model_loader_get_info(loader, MODEL_TYPE_ASR);
        RC_ASSERT(info.loaded == 0);
        RC_ASSERT(static_cast<size_t>(info.memory_usage) == static_cast<size_t>(0));

        RC_ASSERT_NULL(model_loader_get_context(loader, MODEL_TYPE_ASR));

        model_loader_destroy(loader);
        unlink(path.c_str());
    });

    /* Test: load all three model types independently */
    rc::check("load all three model types independently", []() {
        std::string asr_path = create_valid_gguf();
        std::string llm_path = create_valid_gguf();
        std::string tts_path = create_valid_gguf();
        if (asr_path.empty() || llm_path.empty() || tts_path.empty()) return;

        ModelLoader* loader = model_loader_create();

        RC_ASSERT(model_loader_load(loader, asr_path.c_str(), MODEL_TYPE_ASR) == ECHO_OK);
        RC_ASSERT(model_loader_load(loader, llm_path.c_str(), MODEL_TYPE_LLM) == ECHO_OK);
        RC_ASSERT(model_loader_load(loader, tts_path.c_str(), MODEL_TYPE_TTS) == ECHO_OK);

        /* All three are loaded */
        for (int i = 0; i < 3; i++) {
            ModelInfo info = model_loader_get_info(loader, static_cast<ModelType>(i));
            RC_ASSERT(info.loaded == 1);
            RC_ASSERT_NOT_NULL(model_loader_get_context(loader, static_cast<ModelType>(i)));
        }

        model_loader_destroy(loader);
        unlink(asr_path.c_str());
        unlink(llm_path.c_str());
        unlink(tts_path.c_str());
    });

    /* Test: memory reporting tracks file + context */
    rc::check("memory_usage includes file size and context memory", []() {
        std::string path = create_valid_gguf();
        if (path.empty()) return;

        struct stat st;
        stat(path.c_str(), &st);
        size_t expected_file_size = static_cast<size_t>(st.st_size);

        ModelLoader* loader = model_loader_create();
        model_loader_load(loader, path.c_str(), MODEL_TYPE_ASR);

        ModelInfo info = model_loader_get_info(loader, MODEL_TYPE_ASR);
        RC_ASSERT(info.file_size == expected_file_size);
        /* memory_usage = mapped_size + context_memory, should be > file_size */
        RC_ASSERT(info.memory_usage > info.file_size);

        model_loader_destroy(loader);
        unlink(path.c_str());
    });

    /* Test: reload same slot replaces previous model */
    rc::check("loading same slot twice replaces previous model", []() {
        std::string path1 = create_valid_gguf();
        std::string path2 = create_valid_gguf();
        if (path1.empty() || path2.empty()) return;

        ModelLoader* loader = model_loader_create();
        RC_ASSERT(model_loader_load(loader, path1.c_str(), MODEL_TYPE_ASR) == ECHO_OK);
        RC_ASSERT_NOT_NULL(model_loader_get_context(loader, MODEL_TYPE_ASR));

        /* Load again on same slot — should succeed without leaking */
        RC_ASSERT(model_loader_load(loader, path2.c_str(), MODEL_TYPE_ASR) == ECHO_OK);
        RC_ASSERT_NOT_NULL(model_loader_get_context(loader, MODEL_TYPE_ASR));

        /* Model info should reflect the new file */
        ModelInfo info = model_loader_get_info(loader, MODEL_TYPE_ASR);
        RC_ASSERT(info.loaded == 1);

        model_loader_destroy(loader);
        unlink(path1.c_str());
        unlink(path2.c_str());
    });

    /* Test: empty file returns ECHO_ERR_MODEL_INVALID */
    rc::check("empty file returns ECHO_ERR_MODEL_INVALID", []() {
        char tmpl[] = "/tmp/qwen_echo_empty_XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd < 0) return;
        close(fd);

        ModelLoader* loader = model_loader_create();
        int result = model_loader_load(loader, tmpl, MODEL_TYPE_ASR);
        RC_ASSERT(result == ECHO_ERR_MODEL_INVALID);

        model_loader_destroy(loader);
        unlink(tmpl);
    });

    /* Test: get_context for unloaded model returns NULL */
    rc::check("get_context for unloaded model returns NULL", []() {
        ModelLoader* loader = model_loader_create();
        RC_ASSERT_NULL(model_loader_get_context(loader, MODEL_TYPE_LLM));
        model_loader_destroy(loader);
    });

    return 0;
}
