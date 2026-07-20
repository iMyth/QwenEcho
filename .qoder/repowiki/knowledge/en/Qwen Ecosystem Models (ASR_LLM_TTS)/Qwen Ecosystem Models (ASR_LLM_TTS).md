---
kind: external_dependency
name: Qwen Ecosystem Models (ASR/LLM/TTS)
slug: qwen-alibaba
category: external_dependency
category_hints:
    - vendor_identity
    - client_constraint
scope:
    - '**'
---

### Qwen Ecosystem Models

QwenEcho uses three models from Alibaba's Qwen ecosystem, all in GGUF/INT4 format for on-device inference:

- **ASR**: FunASR-Nano (~150MB) - speech recognition supporting 52 languages
- **LLM**: Qwen3-4B-Instruct (~2.2GB) - bilingual translation with context
- **TTS**: Qwen3-TTS-Streaming (~250MB) - streaming text-to-speech synthesis

**Integration Points:**
- Model filenames are centrally defined in `lib/src/model/model_catalog.dart`
- Native engine validates GGUF magic bytes (`0x46475547`) and INT4 quantization in `native/src/model_loader.cpp`
- Models are loaded via memory-mapped files through the FFI bridge

**Constraints:**
- All models must be GGUF format with INT4 quantization (Q4_0, Q4_1, or Q4_K)
- Total disk footprint must not exceed 2.85GB across all three models
- Models are provisioned locally only - no network access after initial setup

**Note:** While Qwen3.5-4B was discussed as a potential upgrade, the current implementation targets Qwen3-4B-Instruct to stay within device memory constraints.