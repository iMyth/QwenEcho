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
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

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
    int       fd;             /* File descriptor (kept open while mapped) */
    void*     mapped_data;    /* mmap base pointer */
    size_t    file_size;      /* File size from stat */
    size_t    mapped_size;    /* Size of the mmap region (== file_size) */
    InferenceContext* ctx;    /* Inference context for this model */
};

/* ─── ModelLoader aggregate ──────────────────────────────────────────────── */
struct ModelLoader {
    ModelSlot slots[3]; /* Indexed by ModelType: ASR=0, LLM=1, TTS=2 */
};

/* ─── Helpers ────────────────────────────────────────────────────────────── */

/**
 * Check whether a GGUF quantization type is an accepted INT4 variant.
 * We accept Q4_0, Q4_1, and Q4_K as valid INT4 formats.
 */
static int is_int4_quant(uint32_t quant_type) {
    return quant_type == GGUF_QUANT_Q4_0 ||
           quant_type == GGUF_QUANT_Q4_1 ||
           quant_type == GGUF_QUANT_Q4_K;
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
 * Skip a single metadata value in a GGUF file buffer.
 *
 * @param data   Pointer to start of file data.
 * @param offset Current byte offset into the data (updated on return).
 * @param size   Total size of the data buffer.
 * @param vtype  The value type to skip.
 * @return 0 on success, -1 if data is truncated.
 */
static int skip_meta_value(const uint8_t* data, size_t* offset, size_t size, uint32_t vtype) {
    size_t scalar_sz = gguf_meta_value_size(vtype);
    if (scalar_sz > 0) {
        if (*offset + scalar_sz > size) return -1;
        *offset += scalar_sz;
        return 0;
    }

    if (vtype == GGUF_META_STRING) {
        /* String: uint64 length + bytes */
        if (*offset + 8 > size) return -1;
        uint64_t slen;
        memcpy(&slen, data + *offset, 8);
        *offset += 8;
        if (*offset + slen > size) return -1;
        *offset += (size_t)slen;
        return 0;
    }

    if (vtype == GGUF_META_ARRAY) {
        /* Array: uint32 elem_type + uint64 count + count * elem_size */
        if (*offset + 4 + 8 > size) return -1;
        uint32_t elem_type;
        memcpy(&elem_type, data + *offset, 4);
        *offset += 4;
        uint64_t count;
        memcpy(&count, data + *offset, 8);
        *offset += 8;

        for (uint64_t i = 0; i < count; i++) {
            if (skip_meta_value(data, offset, size, elem_type) != 0) return -1;
        }
        return 0;
    }

    return -1; /* Unknown type */
}

/**
 * Parse GGUF header and extract the first tensor's quantization type.
 *
 * @param data      Memory-mapped file data.
 * @param size      File size.
 * @param out_quant Output: quantization type of the first tensor.
 * @return 0 on success, -1 on parse error.
 */
static int gguf_get_first_tensor_quant(const uint8_t* data, size_t size, uint32_t* out_quant) {
    /* Header is already validated; skip it */
    if (size < sizeof(GgufHeader)) return -1;

    GgufHeader header;
    memcpy(&header, data, sizeof(GgufHeader));

    size_t offset = sizeof(GgufHeader);

    /* Skip metadata key-value pairs */
    for (uint64_t i = 0; i < header.metadata_kv_count; i++) {
        /* Key: uint64 name_len + name bytes */
        if (offset + 8 > size) return -1;
        uint64_t key_len;
        memcpy(&key_len, data + offset, 8);
        offset += 8;
        if (offset + key_len > size) return -1;
        offset += (size_t)key_len;

        /* Value type */
        if (offset + 4 > size) return -1;
        uint32_t vtype;
        memcpy(&vtype, data + offset, 4);
        offset += 4;

        /* Value */
        if (skip_meta_value(data, &offset, size, vtype) != 0) return -1;
    }

    /* Now at tensor info section. Read the first tensor's type.
     * Tensor info: name_len(u64) + name(bytes) + n_dims(u32) + dims(u64*n_dims) + type(u32) + offset(u64) */
    if (header.tensor_count == 0) return -1;

    /* name length */
    if (offset + 8 > size) return -1;
    uint64_t name_len;
    memcpy(&name_len, data + offset, 8);
    offset += 8;

    /* name bytes */
    if (offset + name_len > size) return -1;
    offset += (size_t)name_len;

    /* n_dims */
    if (offset + 4 > size) return -1;
    uint32_t n_dims;
    memcpy(&n_dims, data + offset, 4);
    offset += 4;

    /* dims array */
    if (offset + (size_t)n_dims * 8 > size) return -1;
    offset += (size_t)n_dims * 8;

    /* tensor type (the quantization type) */
    if (offset + 4 > size) return -1;
    uint32_t tensor_type;
    memcpy(&tensor_type, data + offset, 4);

    *out_quant = tensor_type;
    return 0;
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
        loader->slots[i].fd = -1;
        loader->slots[i].mapped_data = MAP_FAILED;
        loader->slots[i].file_size = 0;
        loader->slots[i].mapped_size = 0;
        loader->slots[i].ctx = nullptr;
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
        if (errno == ENOENT || errno == ENOTDIR) {
            return ECHO_ERR_MODEL_MISSING;
        }
        return ECHO_ERR_MODEL_PERMISSION;
    }

    /* Step 2: Check file is readable */
    if (access(path, R_OK) != 0) {
        return ECHO_ERR_MODEL_PERMISSION;
    }

    /* Ensure it's a regular file with non-zero size */
    if (!S_ISREG(st.st_mode) || st.st_size == 0) {
        return ECHO_ERR_MODEL_INVALID;
    }

    size_t file_size = static_cast<size_t>(st.st_size);

    /* Step 3: Open file and read GGUF magic */
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno == EACCES) return ECHO_ERR_MODEL_PERMISSION;
        return ECHO_ERR_MODEL_MISSING;
    }

    /* Must have at least a header */
    if (file_size < sizeof(GgufHeader)) {
        close(fd);
        return ECHO_ERR_MODEL_INVALID;
    }

    /* Read the header to validate magic */
    GgufHeader header;
    ssize_t nread = read(fd, &header, sizeof(GgufHeader));
    if (nread != static_cast<ssize_t>(sizeof(GgufHeader))) {
        close(fd);
        return ECHO_ERR_MODEL_INVALID;
    }

    if (header.magic != GGUF_MAGIC) {
        close(fd);
        return ECHO_ERR_MODEL_INVALID;
    }

    /* Step 5: Memory-map the file */
    void* mapped = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        return ECHO_ERR_MEMORY;
    }

    /* Advise the kernel for sequential access (optional, best-effort) */
    madvise(mapped, file_size, MADV_SEQUENTIAL);

    /* Step 4: Validate INT4 quantization type from first tensor */
    uint32_t quant_type = 0;
    int parse_result = gguf_get_first_tensor_quant(
        static_cast<const uint8_t*>(mapped), file_size, &quant_type);

    if (parse_result != 0 || !is_int4_quant(quant_type)) {
        munmap(mapped, file_size);
        close(fd);
        return ECHO_ERR_MODEL_INVALID;
    }

    /* Step 6: Create inference context */
    InferenceContext* ctx = create_inference_context(type, file_size);
    if (!ctx) {
        munmap(mapped, file_size);
        close(fd);
        return ECHO_ERR_MEMORY;
    }

    /* Populate the slot */
    slot->loaded = 1;
    slot->fd = fd;
    slot->mapped_data = mapped;
    slot->file_size = file_size;
    slot->mapped_size = file_size;
    slot->ctx = ctx;

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
        /* Memory usage = mapped region + inference context memory */
        info.memory_usage = slot->mapped_size;
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

    /* Unmap file */
    if (slot->mapped_data != MAP_FAILED && slot->mapped_data != nullptr) {
        munmap(slot->mapped_data, slot->mapped_size);
        slot->mapped_data = MAP_FAILED;
    }

    /* Close file descriptor */
    if (slot->fd >= 0) {
        close(slot->fd);
        slot->fd = -1;
    }

    slot->loaded = 0;
    slot->file_size = 0;
    slot->mapped_size = 0;
}

void model_loader_destroy(ModelLoader* loader) {
    if (!loader) return;

    /* Unload all models */
    for (int i = 0; i < 3; i++) {
        model_loader_unload(loader, static_cast<ModelType>(i));
    }

    free(loader);
}
