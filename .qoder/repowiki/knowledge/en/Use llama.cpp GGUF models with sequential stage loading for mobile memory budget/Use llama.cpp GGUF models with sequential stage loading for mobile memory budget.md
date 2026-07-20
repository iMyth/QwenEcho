---
kind: design
name: Use llama.cpp GGUF models with sequential stage loading for mobile memory budget
source: session
category: adr
---

# Use llama.cpp GGUF models with sequential stage loading for mobile memory budget

_Source: coding plans from commit period 67c25c6 → 6342f7d — records intent at planning time; the implementation may lag or differ._

## Context
The native inference layer must replace stub ASR/LLM/TTS functions with real model execution on iOS devices. Mobile memory is severely constrained, and the three models (ASR ~590MB, LLM ~1GB, TTS ~520MB) cannot all be resident simultaneously.

## Decision drivers
- mobile memory budget (~2-3GB total device RAM)
- no shared session or model cache across stages
- single-device deployment

## Considered options
- **Sequential per-stage model loading (load/unload per stage)** — pros: Keeps peak memory under ~1.1GB (largest single model); leverages OS mmap page cache; simplest to implement; no cross-stage synchronization; cons: Reload overhead between stages; slower end-to-end latency; relies on OS eviction policy
- **Shared model pool with reference counting** _(rejected)_ — pros: Avoids reload cost; keeps frequently used models hot in memory; cons: Complex ref-counting and lifecycle management; risk of OOM if all three models stay resident; adds a new failure mode on iOS
- **All models loaded at pipeline start** _(rejected)_ — pros: Zero reload latency between stages; cons: Peak memory ~2.2GB — exceeds safe headroom on many iOS devices; likely to trigger background app termination

## Decision
Each pipeline stage owns its own GgufContext created from a GGUF file path passed via PipelineController; models are loaded when the stage starts and destroyed when the stage finishes, so only one model resides in memory at a time. The OS mmap page cache provides best-effort reuse without explicit caching logic.

## Consequences
End-to-end latency includes model load time between ASR→LLM→TTS transitions. If reload becomes a bottleneck, a future optimization could introduce a bounded shared pool. Stage lifetimes must be carefully managed to ensure contexts are destroyed before the next stage loads.