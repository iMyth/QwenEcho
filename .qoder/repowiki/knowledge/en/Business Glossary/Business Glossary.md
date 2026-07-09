---
kind: business_term
name: Business Glossary
category: business_term
scope:
    - '**'
---

### Air-gapped
- Definition：Operating mode where the app runs entirely offline with zero network connectivity. QwenEcho enforces this at both compile-time (symbol poisoning prevents network API usage) and runtime (no network libraries loaded). The model provisioning feature uses local file import rather than network downloads to maintain this constraint.
- Aliases：offline-only、no-network

### Pipeline
- Definition：The end-to-end audio processing chain that converts speech to translated speech: Audio Capture → ASR Stage → LLM Translation Stage → TTS Stage → Speaker Output. Each stage operates concurrently with cascade truncation, allowing downstream stages to begin before upstream stages complete.
- Aliases：processing pipeline、echo pipeline

### GGUF/INT4
- Definition：The required model file format and quantization scheme. GGUF is the container format, INT4 refers to 4-bit integer quantization applied to reduce model size for mobile deployment. The native loader strictly validates GGUF magic bytes and accepts only Q4_0, Q4_1, or Q4_K quantization types.
- Aliases：GGUF INT4、quantized models

### Thermal Mode
- Definition：A three-state thermal management system: Normal (≤42°C, full performance), Throttle (>43°C, reduced context window and sample rate), and Critical (>50°C, pipeline paused). The system transitions between modes based on hardware temperature polling every 5 seconds.
- Aliases：thermal state、temperature mode

### Split View
- Definition：The bilateral interpretation UI layout showing two speakers face-to-face. The top half is rotated 180 degrees so each speaker sees their own transcription while reading the opposing speaker's translation. Both halves operate independently and simultaneously.
- Aliases：bilateral view、face-to-face view

### Context Window
- Definition：A sliding window of previous translations prepended to maintain coherence across segments. Uses 512 tokens in Normal thermal mode and reduces to 256 tokens in Throttle mode. When combined with new input exceeds the limit, oldest entries are truncated first.
- Aliases：sliding context、translation context
