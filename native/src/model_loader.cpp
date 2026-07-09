/**
 * @file model_loader.cpp
 * @brief Model Loader implementation.
 *
 * Validates GGUF files, memory-maps them via mmap, and creates
 * inference contexts for each model type (ASR, LLM, TTS).
 */

#include "model_loader.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __APPLE__
#include <os/log.h>
#define ECHO_LOG(fmt, ...) do { os_log(OS_LOG_DEFAULT, fmt, ##__VA_ARGS__); fprintf(stderr, fmt "\n", ##__VA_ARGS__); } while(0)
#else
#define ECHO_LOG(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#endif

/* ─── Internal inference context placeholder ─────────────────────────────────
 * In a full integration this would wrap the actual ggml_context.
 * For now it tracks the allocation size for memory reporting.
 */
struct InferenceContext {
    ModelType type;
    size_t    context_memory;  /* Bytes allocated for inference buffers */
};

/* ─── Per-model slot ─────────────────────────────────────────────────────── */
struct ModelSlot {
    int       loaded;          /* 1 = active, 0 = empty */
    size_t    file_size;      /* File size from stat */
    InferenceContext* ctx;    /* Inference context for this model */
    char      path[1024];     /* File path (for stage model loading via llama.cpp) */
};

/* ─── ModelLoader aggregate ──────────────────────────────────────────────── */
struct ModelLoader {
    ModelSlot slots[3]; /* Indexed by ModelType: ASR=0, LLM=1, TTS=2 */
};

/* ─── Helpers ────────────────────────────────────────────────────────────── */

/**
 * Check whether a GGUF quantization type is accepted for on-device use.
 *
 * Accepted quants include:
 *   - Legacy INT4:  Q4_0, Q4_1
 *   - K-quants:     Q3_K, Q4_K, Q5_K, Q6_K  (mobile-friendly, good accuracy)
 *   - IQ variants:  IQ4_NL, IQ4_XS          (importance-matrix 4-bit)
 *
 * We reject F16/F32 (too large for mobile memory budget) and Q2_K
 * (quality too degraded for real-time interpretation).
 */
static int is_accepted_quant(uint32_t quant_type) {
    switch (quant_type) {
        case GGUF_QUANT_Q4_0:
        case GGUF_QUANT_Q4_1:
        case GGUF_QUANT_Q3_K:
        case GGUF_QUANT_Q4_K:
        case GGUF_QUANT_Q5_K:
        case GGUF_QUANT_Q6_K:
        case GGUF_QUANT_IQ4_NL:
        case GGUF_QUANT_IQ4_XS:
            return 1;
        default:
            return 0;
    }
}

/**
 * Read the predominant quantization type from a GGUF file.
 *
 * In GGUF v2/v3 the tensor metadata follows the header KV pairs.
 * For validation we scan the first tensor's type field. In a real
 * implementation we would iterate all tensors; here we check the first
 * tensor type as a representative (production models are uniformly quantized).
 *
 * The GGUF layout after the header KV section is:
 *   For each tensor: name_len(u64), name(bytes), n_dims(u32),
 *                    dims[n_dims](u64 each), type(u32), offset(u64)
 *
 * We simplify by scanning for the first uint32 value after metadata that
 * could be a tensor type. For this project, we use a heuristic: skip
 * the header and metadata, then read the first tensor info block.
 *
 * NOTE: This is a simplified validator. A full implementation would parse
 * all GGUF metadata key-value pairs to locate the general.quantization_version
 * or iterate tensor descriptors. For the MVP we validate:
 * 1. Magic bytes are correct
 * 2. The file is large enough to contain tensor data
 * 3. We scan for the first tensor type field
 */

/**
 * GGUF metadata value types used for KV parsing.
 */
typedef enum {
    GGUF_META_UINT8   = 0,
    GGUF_META_INT8    = 1,
    GGUF_META_UINT16  = 2,
    GGUF_META_INT16   = 3,
    GGUF_META_UINT32  = 4,
    GGUF_META_INT32   = 5,
    GGUF_META_FLOAT32 = 6,
    GGUF_META_BOOL    = 7,
    GGUF_META_STRING  = 8,
    GGUF_META_ARRAY   = 9,
    GGUF_META_UINT64  = 10,
    GGUF_META_INT64   = 11,
    GGUF_META_FLOAT64 = 12,
} GgufMetaValueType;

/**
 * Get size of a scalar GGUF metadata value type in bytes.
 * Returns 0 for variable-length types (string, array).
 */
static size_t gguf_meta_value_size(uint32_t vtype) {
    switch (vtype) {
        case GGUF_META_UINT8:   return 1;
        case GGUF_META_INT8:    return 1;
        case GGUF_META_UINT16:  return 2;
        case GGUF_META_INT16:   return 2;
        case GGUF_META_UINT32:  return 4;
        case GGUF_META_INT32:   return 4;
        case GGUF_META_FLOAT32: return 4;
        case GGUF_META_BOOL:    return 1;
        case GGUF_META_UINT64:  return 8;
        case GGUF_META_INT64:   return 8;
        case GGUF_META_FLOAT64: return 8;
        default:                return 0; /* string / array */
    }
}

/**
 * Map a llama.cpp `general.file_type` (ftype) value to the equivalent
 * per-tensor ggml type, so downstream validation uses a single enum.
 *
 * ftype and ggml_type share values for 0–3 and 10, but DIVERGE for 11+:
 *   ftype 11/12/13 = Q3_K_S/M/L  →  ggml Q3_K (11)
 *   ftype 14/15    = Q4_K_S/M    →  ggml Q4_K (12)
 *   ftype 16/17    = Q5_K_S/M    →  ggml Q5_K (13)
 *   ftype 18       = Q6_K        →  ggml Q6_K (14)
 * Values outside this range (Q4_0=2, Q8_0=7, ...) pass through unchanged.
 */
static uint32_t ftype_to_ggml_type(uint32_t ftype) {
    switch (ftype) {
        case 11: case 12: case 13: return GGUF_QUANT_Q3_K;  /* Q3_K_S/M/L */
        case 14: case 15:         return GGUF_QUANT_Q4_K;  /* Q4_K_S/M   */
        case 16: case 17:         return GGUF_QUANT_Q5_K;  /* Q5_K_S/M   */
        case 18:                   return GGUF_QUANT_Q6_K;  /* Q6_K       */
        default:                   return ftype;            /* passthrough */
    }
}

/**
 * Determine the representative quantization type of a GGUF file.
 *
 * Strategy:
 *   1. Read `general.file_type` from metadata — the canonical field that
 *      conversion tools (llama.cpp/unsloth/bartowski) set to the predominant
 *      quant type. It uses the llama.cpp ftype enum, normalized to the
 *      per-tensor ggml type via ftype_to_ggml_type() before returning.
 *   2. If `general.file_type` is absent (some mixed-precision ASR/TTS models
 *      omit it), scan tensor descriptors for the first tensor whose ggml type
 *      is an accepted quantization. This inherently skips F32/F16 norm & bias
 *      tensors and any non-accepted precision (e.g. BF16 conv weights).
 *
 * Uses read()-based I/O instead of mmap to avoid double-mapping files
 * that llama.cpp will mmap itself during model loading.
 *
 * @param fd        Open file descriptor (read-only).
 * @param out_quant Output: the determined quantization type (ggml enum).
 * @return 0 on success, -1 on parse error or no quantized tensor found.
 */
static int gguf_determine_quant_type_fd(int fd, uint32_t* out_quant) {
    /* Read header from the beginning of the file */
    GgufHeader header;
    if (pread(fd, &header, sizeof(GgufHeader), 0) != sizeof(GgufHeader)) {
        return -1;
    }

    if (header.magic != GGUF_MAGIC) {
        return -1;
    }

    size_t offset = sizeof(GgufHeader);
    int found_file_type = 0;

    /* ── Phase 1: Parse metadata KV pairs, look for general.file_type ───── */
    for (uint64_t i = 0; i < header.metadata_kv_count; i++) {
        /* Key length */
        uint64_t key_len;
        if (pread(fd, &key_len, 8, offset) != 8) return -1;
        offset += 8;

        /* Key bytes */
        char key_buf[256];
        size_t key_sz = (size_t)key_len;
        if (key_sz > sizeof(key_buf)) {
            /* Key too long — skip it */
            offset += key_sz;
            /* Read value type to skip value */
            uint32_t vtype;
            if (pread(fd, &vtype, 4, offset) != 4) return -1;
            offset += 4;
            /* Skip value using a temporary buffer approach */
            /* For very long keys, just skip — we only care about known short keys */
            continue;
        }
        if (pread(fd, key_buf, key_sz, offset) != (ssize_t)key_sz) return -1;
        offset += key_sz;

        /* Value type */
        uint32_t vtype;
        if (pread(fd, &vtype, 4, offset) != 4) return -1;
        offset += 4;

        /* Check if this is general.file_type (uint32 value) */
        if (!found_file_type &&
            key_sz == 17 &&
            memcmp(key_buf, "general.file_type", 17) == 0 &&
            vtype == GGUF_META_UINT32) {
            uint32_t file_type;
            if (pread(fd, &file_type, 4, offset) != 4) return -1;
            *out_quant = ftype_to_ggml_type(file_type);
            found_file_type = 1;
        }

        /* Skip the value — we need to read enough to skip variable-length types */
        if (vtype == GGUF_META_STRING) {
            uint64_t slen;
            if (pread(fd, &slen, 8, offset) != 8) return -1;
            offset += 8 + (size_t)slen;
        } else if (vtype == GGUF_META_ARRAY) {
            uint32_t elem_type;
            uint64_t count;
            if (pread(fd, &elem_type, 4, offset) != 4) return -1;
            offset += 4;
            if (pread(fd, &count, 8, offset) != 8) return -1;
            offset += 8;
            /* For arrays of strings, we must iterate to compute total skip */
            for (uint64_t j = 0; j < count; j++) {
                if (elem_type == GGUF_META_STRING) {
                    uint64_t slen;
                    if (pread(fd, &slen, 8, offset) != 8) return -1;
                    offset += 8 + (size_t)slen;
                } else {
                    size_t sz = gguf_meta_value_size(elem_type);
                    if (sz == 0) return -1;
                    offset += sz;
                }
            }
        } else {
            size_t sz = gguf_meta_value_size(vtype);
            if (sz == 0) return -1;
            offset += sz;
        }
    }

    if (found_file_type) return 0;

    /* ── Phase 2: Scan tensors for the first quantized weight ───────────── */
    if (header.tensor_count == 0) return -1;

    for (uint64_t t = 0; t < header.tensor_count; t++) {
        /* name length + name bytes */
        uint64_t name_len;
        if (pread(fd, &name_len, 8, offset) != 8) return -1;
        offset += 8 + (size_t)name_len;

        /* n_dims */
        uint32_t n_dims;
        if (pread(fd, &n_dims, 4, offset) != 4) return -1;
        offset += 4 + (size_t)n_dims * 8;

        /* tensor type */
        uint32_t tensor_type;
        if (pread(fd, &tensor_type, 4, offset) != 4) return -1;
        offset += 4;

        /* tensor data offset */
        offset += 8;

        if (is_accepted_quant(tensor_type)) {
            *out_quant = tensor_type;
            return 0;
        }
    }

    return -1;
}

/**
 * Create a placeholder inference context.
 * In production this would allocate ggml compute buffers.
 */
static InferenceContext* create_inference_context(ModelType type, size_t model_size) {
    auto* ctx = static_cast<InferenceContext*>(malloc(sizeof(InferenceContext)));
    if (!ctx) return nullptr;

    ctx->type = type;

    /* Estimate inference context memory based on model type.
     * These are placeholder values; real implementation would use ggml_init. */
    switch (type) {
        case MODEL_TYPE_ASR:
            ctx->context_memory = 64 * 1024 * 1024;   /* ~64 MB for ASR buffers */
            break;
        case MODEL_TYPE_LLM:
            ctx->context_memory = 128 * 1024 * 1024;  /* ~128 MB for LLM KV cache */
            break;
        case MODEL_TYPE_TTS:
            ctx->context_memory = 32 * 1024 * 1024;   /* ~32 MB for TTS buffers */
            break;
        default:
            ctx->context_memory = 32 * 1024 * 1024;
            break;
    }
    (void)model_size;
    return ctx;
}

/* ─── Public API ─────────────────────────────────────────────────────────── */

ModelLoader* model_loader_create(void) {
    auto* loader = static_cast<ModelLoader*>(calloc(1, sizeof(ModelLoader)));
    if (!loader) return nullptr;

    for (int i = 0; i < 3; i++) {
        loader->slots[i].loaded = 0;
        loader->slots[i].file_size = 0;
        loader->slots[i].ctx = nullptr;
        loader->slots[i].path[0] = '\0';
    }
    return loader;
}

int model_loader_load(ModelLoader* loader, const char* path, ModelType type) {
    if (!loader || !path) return ECHO_ERR_MODEL_MISSING;
    if (type < MODEL_TYPE_ASR || type > MODEL_TYPE_TTS) return ECHO_ERR_MODEL_INVALID;

    ModelSlot* slot = &loader->slots[type];

    /* If already loaded, unload first */
    if (slot->loaded) {
        model_loader_unload(loader, type);
    }

    /* Step 1: Check file exists */
    struct stat st;
    if (stat(path, &st) != 0) {
        ECHO_LOG("[ModelLoader] stat failed for: %s (errno=%d)", path, errno);
        if (errno == ENOENT || errno == ENOTDIR) {
            return ECHO_ERR_MODEL_MISSING;
        }
        return ECHO_ERR_MODEL_PERMISSION;
    }

    /* Step 2: Check file is readable */
    if (access(path, R_OK) != 0) {
        ECHO_LOG("[ModelLoader] access denied: %s", path);
        return ECHO_ERR_MODEL_PERMISSION;
    }

    /* Ensure it's a regular file with non-zero size */
    if (!S_ISREG(st.st_mode) || st.st_size == 0) {
        ECHO_LOG("[ModelLoader] not a regular file or empty: %s", path);
        return ECHO_ERR_MODEL_INVALID;
    }

    size_t file_size = static_cast<size_t>(st.st_size);

    /* Step 3: Open file and validate GGUF header via read() I/O.
     * We do NOT mmap the file here — llama.cpp will mmap it during
     * gguf_inference_create() / llama_model_load_from_file().
     * Double-mapping large files (1GB+) on iOS causes memory pressure
     * and mmap failures. */
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        ECHO_LOG("[ModelLoader] fopen failed: %s (errno=%d)", path, errno);
        if (errno == EACCES) return ECHO_ERR_MODEL_PERMISSION;
        return ECHO_ERR_MODEL_MISSING;
    }

    /* Must have at least a header */
    if (file_size < sizeof(GgufHeader)) {
        fclose(fp);
        return ECHO_ERR_MODEL_INVALID;
    }

    /* Read the header to validate magic */
    GgufHeader header;
    if (fread(&header, 1, sizeof(GgufHeader), fp) != sizeof(GgufHeader)) {
        fclose(fp);
        return ECHO_ERR_MODEL_INVALID;
    }

    if (header.magic != GGUF_MAGIC) {
        ECHO_LOG("[ModelLoader] bad GGUF magic: 0x%08X (expected 0x%08X)",
                 header.magic, GGUF_MAGIC);
        fclose(fp);
        return ECHO_ERR_MODEL_INVALID;
    }

    /* Step 4: Determine quantization type and validate using fd-based parsing */
    int fd = fileno(fp);
    uint32_t quant_type = 0;
    int parse_result = gguf_determine_quant_type_fd(fd, &quant_type);
    fclose(fp);  /* Close file — llama.cpp will open it again */
    fp = nullptr;

    if (parse_result != 0) {
        ECHO_LOG("[ModelLoader] failed to determine quant type: %s", path);
        return ECHO_ERR_MODEL_INVALID;
    }

    if (!is_accepted_quant(quant_type)) {
        ECHO_LOG("[ModelLoader] rejected quant type %u: %s", quant_type, path);
        return ECHO_ERR_MODEL_INVALID;
    }

    /* Step 5: Create placeholder inference context */
    InferenceContext* ctx = create_inference_context(type, file_size);
    if (!ctx) {
        return ECHO_ERR_MEMORY;
    }

    /* Populate the slot */
    slot->loaded = 1;
    slot->file_size = file_size;
    slot->ctx = ctx;

    /* Store path for later retrieval by pipeline stages */
    strncpy(slot->path, path, sizeof(slot->path) - 1);
    slot->path[sizeof(slot->path) - 1] = '\0';

    const char* type_name = (type == MODEL_TYPE_ASR) ? "ASR" :
                            (type == MODEL_TYPE_LLM) ? "LLM" : "TTS";
    ECHO_LOG("[ModelLoader] %s model validated: %zu bytes (%.1f MB), quant=%u, path=%s",
             type_name, file_size, file_size / (1024.0 * 1024.0), quant_type, path);

    return ECHO_OK;
}

ModelInfo model_loader_get_info(const ModelLoader* loader, ModelType type) {
    ModelInfo info;
    memset(&info, 0, sizeof(info));
    info.type = type;

    if (!loader || type < MODEL_TYPE_ASR || type > MODEL_TYPE_TTS) {
        return info;
    }

    const ModelSlot* slot = &loader->slots[type];
    info.loaded = slot->loaded;

    if (slot->loaded) {
        info.file_size = slot->file_size;
        /* Memory usage = inference context memory (llama.cpp manages its own mmap) */
        info.memory_usage = slot->file_size;  /* file will be mmap'd by llama.cpp */
        if (slot->ctx) {
            info.memory_usage += slot->ctx->context_memory;
        }
    }

    return info;
}

void* model_loader_get_context(ModelLoader* loader, ModelType type) {
    if (!loader || type < MODEL_TYPE_ASR || type > MODEL_TYPE_TTS) {
        return nullptr;
    }

    ModelSlot* slot = &loader->slots[type];
    if (!slot->loaded || !slot->ctx) {
        return nullptr;
    }

    return static_cast<void*>(slot->ctx);
}

const char* model_loader_get_path(const ModelLoader* loader, ModelType type) {
    if (!loader || type < MODEL_TYPE_ASR || type > MODEL_TYPE_TTS) {
        return nullptr;
    }

    const ModelSlot* slot = &loader->slots[type];
    if (!slot->loaded || slot->path[0] == '\0') {
        return nullptr;
    }

    return slot->path;
}

void model_loader_unload(ModelLoader* loader, ModelType type) {
    if (!loader || type < MODEL_TYPE_ASR || type > MODEL_TYPE_TTS) {
        return;
    }

    ModelSlot* slot = &loader->slots[type];
    if (!slot->loaded) return;

    /* Release inference context */
    if (slot->ctx) {
        free(slot->ctx);
        slot->ctx = nullptr;
    }

    slot->loaded = 0;
    slot->file_size = 0;
    slot->path[0] = '\0';
}

void model_loader_destroy(ModelLoader* loader) {
    if (!loader) return;

    /* Unload all models */
    for (int i = 0; i < 3; i++) {
        model_loader_unload(loader, static_cast<ModelType>(i));
    }

    free(loader);
}
