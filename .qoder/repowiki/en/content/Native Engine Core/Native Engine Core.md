# Native Engine Core

<cite>
**Referenced Files in This Document**
- [engine_manager.h](file://native/include/engine_manager.h)
- [engine_manager.cpp](file://native/src/engine_manager.cpp)
- [pipeline_controller.h](file://native/include/pipeline_controller.h)
- [pipeline_controller.cpp](file://native/src/pipeline_controller.cpp)
- [native_port.h](file://native/include/native_port.h)
- [native_port.cpp](file://native/src/native_port.cpp)
- [echo_types.h](file://native/include/echo_types.h)
- [bounded_spsc_queue.h](file://native/include/bounded_spsc_queue.h)
- [audio_ring_buffer.h](file://native/include/audio_ring_buffer.h)
- [ffi_bridge.h](file://native/include/ffi_bridge.h)
- [ffi_bridge.cpp](file://native/src/ffi_bridge.cpp)
- [asr_stage.h](file://native/include/asr_stage.h)
- [asr_stage.cpp](file://native/src/asr_stage.cpp)
- [llm_stage.h](file://native/include/llm_stage.h)
- [llm_stage.cpp](file://native/src/llm_stage.cpp)
- [tts_stage.h](file://native/include/tts_stage.h)
- [sentence_segmenter.h](file://native/include/sentence_segmenter.h)
- [gguf_inference.h](file://native/include/gguf_inference.h)
- [gguf_inference.cpp](file://native/src/gguf_inference.cpp)
</cite>

## Update Summary
**Changes Made**
- Added comprehensive documentation for the new GGUF inference backend system
- Updated LLM stage documentation to reflect real GGUF model integration
- Enhanced ASR stage documentation with GGUF inference capabilities
- Added new section covering GGUF inference architecture and API
- Updated dependency analysis to include GGUF inference integration
- Enhanced performance considerations with GGUF-specific optimizations

## Table of Contents
1. [Introduction](#introduction)
2. [Project Structure](#project-structure)
3. [Core Components](#core-components)
4. [Architecture Overview](#architecture-overview)
5. [GGUF Inference Backend](#gguf-inference-backend)
6. [Detailed Component Analysis](#detailed-component-analysis)
7. [Dependency Analysis](#dependency-analysis)
8. [Performance Considerations](#performance-considerations)
9. [Troubleshooting Guide](#troubleshooting-guide)
10. [Conclusion](#conclusion)
11. [Appendices](#appendices)

## Introduction
This document explains the high-performance audio processing pipeline in QwenEcho's C/C++ native engine core. It focuses on:
- EngineManager lifecycle state machine and session control
- PipelineController orchestration of sequential stages with lock-free communication
- NativePort asynchronous Dart-to-native message delivery
- EchoTypes shared data structures
- **New**: Complete GGUF inference backend using llama.cpp for real AI model execution
- Error handling, resource management, threading models
- Extensibility for custom stages and new AI models
- Performance, memory management, and debugging techniques

## Project Structure
The native layer is organized into headers under include/ and implementations under src/. Key modules:
- FFI bridge exposes a minimal C API to Flutter via Dart FFI
- EngineManager coordinates model loading and pipeline lifecycle
- PipelineController constructs and manages all pipeline components and threads
- Stages (ASR, LLM, TTS) perform inference and stream results
- **New**: GGUF inference backend provides thin C wrapper around llama.cpp
- Lock-free primitives (ring buffer, bounded SPSC queue) connect stages
- NativePort posts typed messages back to Dart asynchronously

```mermaid
graph TB
subgraph "FFI Layer"
FFI["ffi_bridge.h/cpp"]
end
subgraph "Engine Core"
EM["engine_manager.h/cpp"]
PC["pipeline_controller.h/cpp"]
STAGES["asr_stage.h/cpp<br/>llm_stage.h/cpp<br/>tts_stage.h"]
SEG["sentence_segmenter.h"]
end
subgraph "AI Inference"
GGUF["gguf_inference.h/cpp<br/>llama.cpp wrapper"]
end
subgraph "Primitives"
RB["audio_ring_buffer.h"]
Q["bounded_spsc_queue.h"]
end
subgraph "Messaging"
NP["native_port.h/cpp"]
ET["echo_types.h"]
end
FFI --> EM
EM --> PC
PC --> RB
PC --> Q
PC --> STAGES
PC --> SEG
STAGES --> NP
STAGES --> GGUF
GGUF --> ET
NP --> ET
```

**Diagram sources**
- [ffi_bridge.h:1-84](file://native/include/ffi_bridge.h#L1-L84)
- [ffi_bridge.cpp:1-124](file://native/src/ffi_bridge.cpp#L1-L124)
- [engine_manager.h:1-104](file://native/include/engine_manager.h#L1-L104)
- [engine_manager.cpp:1-202](file://native/src/engine_manager.cpp#L1-L202)
- [pipeline_controller.h:1-107](file://native/include/pipeline_controller.h#L1-L107)
- [pipeline_controller.cpp:1-488](file://native/src/pipeline_controller.cpp#L1-L488)
- [asr_stage.h:1-104](file://native/include/asr_stage.h#L1-L104)
- [asr_stage.cpp:1-407](file://native/src/asr_stage.cpp#L1-407)
- [llm_stage.h:1-93](file://native/include/llm_stage.h#L1-L93)
- [llm_stage.cpp:1-470](file://native/src/llm_stage.cpp#L1-470)
- [tts_stage.h:1-79](file://native/include/tts_stage.h#L1-L79)
- [sentence_segmenter.h:1-142](file://native/include/sentence_segmenter.h#L1-L142)
- [audio_ring_buffer.h:1-192](file://native/include/audio_ring_buffer.h#L1-L192)
- [bounded_spsc_queue.h:1-145](file://native/include/bounded_spsc_queue.h#L1-L145)
- [native_port.h:1-179](file://native/include/native_port.h#L1-L179)
- [native_port.cpp:1-320](file://native/src/native_port.cpp#L1-L320)
- [echo_types.h:1-136](file://native/include/echo_types.h#L1-L136)
- [gguf_inference.h:1-98](file://native/include/gguf_inference.h#L1-98)
- [gguf_inference.cpp:1-346](file://native/src/gguf_inference.cpp#L1-346)

**Section sources**
- [ffi_bridge.h:1-84](file://native/include/ffi_bridge.h#L1-L84)
- [ffi_bridge.cpp:1-124](file://native/src/ffi_bridge.cpp#L1-L124)
- [engine_manager.h:1-104](file://native/include/engine_manager.h#L1-L104)
- [engine_manager.cpp:1-202](file://native/src/engine_manager.cpp#L1-L202)
- [pipeline_controller.h:1-107](file://native/include/pipeline_controller.h#L1-L107)
- [pipeline_controller.cpp:1-488](file://native/src/pipeline_controller.cpp#L1-L488)
- [asr_stage.h:1-104](file://native/include/asr_stage.h#L1-L104)
- [asr_stage.cpp:1-407](file://native/src/asr_stage.cpp#L1-407)
- [llm_stage.h:1-93](file://native/include/llm_stage.h#L1-L93)
- [llm_stage.cpp:1-470](file://native/src/llm_stage.cpp#L1-470)
- [tts_stage.h:1-79](file://native/include/tts_stage.h#L1-L79)
- [sentence_segmenter.h:1-142](file://native/include/sentence_segmenter.h#L1-L142)
- [audio_ring_buffer.h:1-192](file://native/include/audio_ring_buffer.h#L1-L192)
- [bounded_spsc_queue.h:1-145](file://native/include/bounded_spsc_queue.h#L1-L145)
- [native_port.h:1-179](file://native/include/native_port.h#L1-L179)
- [native_port.cpp:1-320](file://native/src/native_port.cpp#L1-L320)
- [echo_types.h:1-136](file://native/include/echo_types.h#L1-L136)
- [gguf_inference.h:1-98](file://native/include/gguf_inference.h#L1-98)
- [gguf_inference.cpp:1-346](file://native/src/gguf_inference.cpp#L1-346)

## Core Components
- EngineManager: Central coordinator for lifecycle, model loading, and pipeline orchestration. Implements a strict state machine and guards invalid transitions.
- PipelineController: Orchestrates creation, startup, and graceful shutdown of all pipeline components; wires ring buffer, queues, stages, monitors, and ensures cascade truncation for low latency.
- NativePort: Asynchronous Dart-to-native messaging system that serializes typed messages and posts them via a registered Dart port.
- EchoTypes: Shared enums and structs for engine states, error codes, inter-stage elements, and configuration.
- **New**: GGUF Inference Backend: Thin C wrapper around llama.cpp providing GGUF model loading, context management, streaming token callbacks, and batch generation with configurable context windows and thread counts.
- Lock-Free Primitives: AudioRingBuffer (SPSC circular buffer) and BoundedSPSCQueue (overflow-drop semantics) provide non-blocking communication between stages.

Key responsibilities and interactions are detailed in subsequent sections.

**Section sources**
- [engine_manager.h:1-104](file://native/include/engine_manager.h#L1-L104)
- [engine_manager.cpp:1-202](file://native/src/engine_manager.cpp#L1-L202)
- [pipeline_controller.h:1-107](file://native/include/pipeline_controller.h#L1-L107)
- [pipeline_controller.cpp:1-488](file://native/src/pipeline_controller.cpp#L1-L488)
- [native_port.h:1-179](file://native/include/native_port.h#L1-L179)
- [native_port.cpp:1-320](file://native/src/native_port.cpp#L1-L320)
- [echo_types.h:1-136](file://native/include/echo_types.h#L1-L136)
- [gguf_inference.h:1-98](file://native/include/gguf_inference.h#L1-98)
- [gguf_inference.cpp:1-346](file://native/src/gguf_inference.cpp#L1-346)
- [audio_ring_buffer.h:1-192](file://native/include/audio_ring_buffer.h#L1-L192)
- [bounded_spsc_queue.h:1-145](file://native/include/bounded_spsc_queue.h#L1-L145)

## Architecture Overview
The pipeline follows a cascaded, overlapped execution model with real AI model inference:
- AudioCollector writes PCM into AudioRingBuffer
- SentenceSegmenter consumes from the ring buffer and locks segments
- ASR stage processes locked segments using GGUF models, streams partials, enqueues confirmed text
- LLM stage translates using GGUF models and emits partial tokens at punctuation boundaries
- TTS stage synthesizes streaming audio chunks
- Monitors observe thermal and memory conditions and adjust behavior
- LatencyTracker measures per-segment and E2E latencies

```mermaid
sequenceDiagram
participant Dart as "Flutter/Dart"
participant FFI as "FFI Bridge"
participant EM as "EngineManager"
participant PC as "PipelineController"
participant RB as "AudioRingBuffer"
participant SEG as "SentenceSegmenter"
participant ASR as "ASR Stage"
participant LLM as "LLM Stage"
participant GGUF as "GGUF Inference"
participant TTS as "TTS Stage"
participant NP as "NativePort"
Dart->>FFI : RegisterEchoMessagePort(port_id)
FFI->>NP : native_port_register(port_id)
Dart->>FFI : InitQwenEchoEngine(asr,llm,tts)
FFI->>EM : engine_manager_load_models(...)
EM->>GGUF : gguf_inference_backend_init()
EM-->>FFI : ECHO_OK or error
Dart->>FFI : StartEchoPipeline(src,tgt)
FFI->>EM : engine_manager_start_pipeline(...)
EM->>PC : pipeline_controller_start(...)
PC->>RB : create ring buffer
PC->>SEG : create segmenter
PC->>ASR : create stage with GGUF model
PC->>LLM : create stage with GGUF model
PC->>TTS : create stage
Note over PC,TTS : All stages run on separate threads
loop Streaming
RB-->>SEG : read frames
SEG-->>ASR : LockedSegment callback
ASR->>GGUF : gguf_inference_generate(prompt)
GGUF-->>ASR : transcription result
ASR-->>NP : MSG_ASR_PARTIAL / MSG_ASR_CONFIRMED
ASR-->>LLM : AsrToLlmElement
LLM->>GGUF : gguf_inference_generate(translation prompt)
GGUF-->>LLM : translation tokens
LLM-->>NP : MSG_TRANSLATION_STREAM / MSG_TRANSLATION_DONE
LLM-->>TTS : LlmToTtsElement
TTS-->>NP : MSG_TTS_STARTED / MSG_TTS_COMPLETE
end
Dart->>FFI : StopEchoPipeline()
FFI->>EM : engine_manager_stop_pipeline()
EM->>PC : pipeline_controller_stop()
PC->>RB : stop collector, flush, discard unlocked
EM->>GGUF : gguf_inference_backend_free()
PC-->>EM : done
EM-->>FFI : ECHO_OK
```

**Diagram sources**
- [ffi_bridge.h:1-84](file://native/include/ffi_bridge.h#L1-L84)
- [ffi_bridge.cpp:1-124](file://native/src/ffi_bridge.cpp#L1-L124)
- [engine_manager.h:1-104](file://native/include/engine_manager.h#L1-L104)
- [engine_manager.cpp:1-202](file://native/src/engine_manager.cpp#L1-L202)
- [pipeline_controller.h:1-107](file://native/include/pipeline_controller.h#L1-L107)
- [pipeline_controller.cpp:1-488](file://native/src/pipeline_controller.cpp#L1-L488)
- [audio_ring_buffer.h:1-192](file://native/include/audio_ring_buffer.h#L1-L192)
- [sentence_segmenter.h:1-142](file://native/include/sentence_segmenter.h#L1-L142)
- [asr_stage.h:1-104](file://native/include/asr_stage.h#L1-L104)
- [asr_stage.cpp:130-206](file://native/src/asr_stage.cpp#L130-L206)
- [llm_stage.h:1-93](file://native/include/llm_stage.h#L1-L93)
- [llm_stage.cpp:178-248](file://native/src/llm_stage.cpp#L178-L248)
- [tts_stage.h:1-79](file://native/include/tts_stage.h#L1-L79)
- [native_port.h:1-179](file://native/include/native_port.h#L1-L179)
- [native_port.cpp:1-320](file://native/src/native_port.cpp#L1-L320)
- [gguf_inference.h:1-98](file://native/include/gguf_inference.h#L1-98)
- [gguf_inference.cpp:1-346](file://native/src/gguf_inference.cpp#L1-346)

## GGUF Inference Backend

### Overview and Architecture
The GGUF inference backend provides a thin C wrapper around llama.cpp, enabling real AI model execution within the QwenEcho pipeline. It offers a simplified API for loading GGUF models and running text-to-text generation with support for both batch and streaming inference modes.

### Core API Functions
The backend exposes a clean C interface with the following key functions:

- **Backend Lifecycle**: `gguf_inference_backend_init()` and `gguf_inference_backend_free()` manage the global llama.cpp backend initialization and cleanup
- **Context Management**: `gguf_inference_create()` loads GGUF models with configurable context windows and thread counts, while `gguf_inference_destroy()` handles proper resource cleanup
- **Inference Modes**: 
  - Batch mode: `gguf_inference_generate()` returns complete output as a string
  - Streaming mode: `gguf_inference_generate_streaming()` invokes per-token callbacks for real-time processing
- **State Management**: `gguf_inference_reset()` clears KV cache between independent inference runs

### Implementation Details
The backend implements several optimization strategies:

- **Memory Efficiency**: Uses mmap for efficient memory usage on mobile platforms
- **Context Window Management**: Automatically caps context size at 2048 tokens for mobile devices
- **Thread Configuration**: Supports configurable CPU thread counts with auto-detection fallback
- **Greedy Sampling**: Uses deterministic greedy sampling suitable for translation and ASR tasks
- **Batch Processing**: Employs optimized batch sizes (512 tokens) for prompt processing

### Integration with Pipeline Stages
Both ASR and LLM stages integrate seamlessly with the GGUF backend:

- **ASR Stage**: Uses GGUF models for speech-to-text transcription with energy-based noise detection
- **LLM Stage**: Leverages GGUF models for real-time translation with sliding context window management
- **Fallback Mechanism**: Both stages gracefully fall back to stub modes when GGUF models fail to load

```mermaid
classDiagram
class GgufContext {
+model : llama_model*
+ctx : llama_context*
+sampler : llama_sampler*
+n_threads : int
+n_ctx : int
}
class GGUF_Inference_API {
+gguf_inference_backend_init() void
+gguf_inference_backend_free() void
+gguf_inference_create(model_path, n_ctx, n_threads) GgufContext*
+gguf_inference_destroy(ctx) void
+gguf_inference_generate(ctx, prompt, output, cap, max_tokens) int
+gguf_inference_generate_streaming(ctx, prompt, callback, user_data, max_tokens) int
+gguf_inference_reset(ctx) void
}
class Token_Callback {
<<function pointer>>
(token, token_len, user_data) int
}
GgufContext <.. GGUF_Inference_API : "managed by"
Token_Callback <.. GGUF_Inference_API : "used by streaming"
```

**Diagram sources**
- [gguf_inference.h:19-91](file://native/include/gguf_inference.h#L19-L91)
- [gguf_inference.cpp:30-146](file://native/src/gguf_inference.cpp#L30-L146)

**Section sources**
- [gguf_inference.h:1-98](file://native/include/gguf_inference.h#L1-L98)
- [gguf_inference.cpp:1-346](file://native/src/gguf_inference.cpp#L1-346)

## Detailed Component Analysis

### EngineManager State Machine and Lifecycle
- States: Uninitialized → Initializing → Ready → Running → Stopping → Ready; error path: Initializing → Error; Error → Uninitialized on reset/destroy.
- Guards:
  - Load models only when Uninitialized
  - Start pipeline only when Ready and no active session
  - Stop pipeline is a no-op if not running
- Responsibilities:
  - Own ModelLoader and PipelineController instances
  - Enforce state transitions and session flags
  - Ensure safe destruction order and mutex lifetime
  - **New**: Initialize and manage GGUF inference backend lifecycle

```mermaid
stateDiagram-v2
[*] --> Uninitialized
Uninitialized --> Initializing : "load_models()"
Initializing --> Ready : "all models loaded + GGUF backend init"
Initializing --> Error : "load failure"
Error --> Uninitialized : "reset/destroy"
Ready --> Running : "start_pipeline()"
Running --> Stopping : "stop_pipeline()"
Stopping --> Ready : "graceful stop complete"
```

**Diagram sources**
- [engine_manager.h:1-104](file://native/include/engine_manager.h#L1-L104)
- [engine_manager.cpp:65-240](file://native/src/engine_manager.cpp#L65-L240)

**Section sources**
- [engine_manager.h:1-104](file://native/include/engine_manager.h#L1-L104)
- [engine_manager.cpp:65-240](file://native/src/engine_manager.cpp#L65-L240)

### PipelineController Orchestration and Graceful Shutdown
- Creates and starts: ring buffer, bounded queues, audio collector, sentence segmenter, ASR/LLM/TTS stages, thermal/memory monitors, latency tracker.
- Cascade truncation:
  - ASR→LLM queue delivers confirmed text immediately; LLM begins translation without waiting for full segment capture.
  - LLM→TTS queue receives partial translations at punctuation; TTS begins synthesis while LLM continues.
- Graceful stop sequence:
  1) Stop AudioCollector
  2) Wait for locked segments to flush through ASR→LLM→TTS within 2 seconds
  3) Destroy all stages and threads
  4) Discard unlocked audio in ring buffer
  5) Release resources

```mermaid
flowchart TD
Start(["Start"]) --> ValidateLang["Validate language pair"]
ValidateLang --> CreateRes["Create ring buffer, queues, stages, monitors"]
CreateRes --> StartMonitors["Start Thermal/Memory monitors"]
StartMonitors --> StartCollector["Start Audio Collector"]
StartCollector --> Running["Pipeline Running"]
Running --> StopReq{"Stop requested?"}
StopReq --> |No| Running
StopReq --> |Yes| Step1["Stop Audio Collector"]
Step1 --> Flush["Flush locked segments (2s deadline)"]
Flush --> DestroyStages["Destroy stages and threads"]
DestroyStages --> Discard["Discard unlocked audio"]
Discard --> Release["Release resources"]
Release --> End(["Done"])
```

**Diagram sources**
- [pipeline_controller.h:1-107](file://native/include/pipeline_controller.h#L1-L107)
- [pipeline_controller.cpp:1-488](file://native/src/pipeline_controller.cpp#L1-L488)

**Section sources**
- [pipeline_controller.h:1-107](file://native/include/pipeline_controller.h#L1-L107)
- [pipeline_controller.cpp:1-488](file://native/src/pipeline_controller.cpp#L1-L488)

### NativePort System for Asynchronous Dart-to-Native Messaging
- Registration:
  - FFI bridge stores Dart port ID and forwards to NativePort
  - Only the most recently registered port receives messages
- Message dispatch:
  - Each post function builds a Dart_CObject array with a type tag and payload
  - Uses atomic state for port registration and runtime-set post function pointer
- Supported messages include ASR partial/confirmed, translation stream/done, TTS started/complete, errors, thermal state, memory warnings, latency warnings, sample drops

```mermaid
classDiagram
class NativePort {
+register(port_id) void
+is_registered() bool
+set_post_fn(fn) void
+post_asr_partial(speaker,text,ts) bool
+post_asr_confirmed(speaker,text,ts,segId) bool
+post_translation_stream(speaker,token,segId) bool
+post_translation_done(speaker,text,segId) bool
+post_tts_started(speaker,segId) bool
+post_tts_complete(speaker,segId) bool
+post_error(code,model,detail) bool
+post_thermal_state(mode,temp) bool
+post_memory_warning(cur,limit,level) bool
+post_latency_warning(stage,actual,budget) bool
+post_sample_drop(dropped,ts) bool
}
class EchoTypes {
<<enum MessageType>>
<<struct AsrToLlmElement>>
<<struct LlmToTtsElement>>
}
NativePort --> EchoTypes : "uses tags and payloads"
```

**Diagram sources**
- [native_port.h:1-179](file://native/include/native_port.h#L1-L179)
- [native_port.cpp:1-320](file://native/src/native_port.cpp#L1-L320)
- [echo_types.h:1-136](file://native/include/echo_types.h#L1-L136)

**Section sources**
- [native_port.h:1-179](file://native/include/native_port.h#L1-L179)
- [native_port.cpp:1-320](file://native/src/native_port.cpp#L1-L320)
- [echo_types.h:1-136](file://native/include/echo_types.h#L1-L136)

### EchoTypes Shared Data Structures
- EngineState: lifecycle states used by EngineManager
- MessageType: tags for NativePort messages
- EchoErrorCode: standardized return codes across FFI entry points
- Inter-stage elements:
  - AsrToLlmElement: segment_id, speaker_id, text, length, timestamp
  - LlmToTtsElement: segment_id, speaker_id, translated text, length, timestamp
- EngineConfig: model paths, pipeline parameters, thresholds, and defaults

These types ensure consistent contracts between stages and messaging.

**Section sources**
- [echo_types.h:1-136](file://native/include/echo_types.h#L1-L136)

### Lock-Free Communication Primitives
- AudioRingBuffer:
  - SPSC circular buffer for PCM samples
  - Overwrite policy: advances read pointer on overflow to avoid blocking producer
  - Cache-line alignment for write/read positions to prevent false sharing
- BoundedSPSCQueue:
  - Fixed capacity power-of-two with bitmask indexing
  - Sequence/turn protocol for occupancy tracking
  - Overflow-drop semantics: drops oldest element when full, never blocks producer
  - Atomic head/tail with acquire/release ordering

```mermaid
classDiagram
class AudioRingBuffer {
+write(samples,count) uint32
+read(dest,count) uint32
+available() uint32
+advance_read_on_overflow(count) void
+capacity() uint32
}
class BoundedSPSCQueue~T,Capacity~ {
+try_push(item) bool
+try_pop(out) bool
+size() uint32
}
AudioRingBuffer <.. BoundedSPSCQueue : "used by pipeline stages"
```

**Diagram sources**
- [audio_ring_buffer.h:1-192](file://native/include/audio_ring_buffer.h#L1-L192)
- [bounded_spsc_queue.h:1-145](file://native/include/bounded_spsc_queue.h#L1-L145)

**Section sources**
- [audio_ring_buffer.h:1-192](file://native/include/audio_ring_buffer.h#L1-L192)
- [bounded_spsc_queue.h:1-145](file://native/include/bounded_spsc_queue.h#L1-L145)

### Stages and Segmenter with GGUF Integration
- SentenceSegmenter:
  - Energy-based VAD and FSMN-VAD simulation
  - State machine: Idle → Accumulating → Locking → Idle
  - Lock conditions: silence threshold, punctuation notification, max duration
- ASR Stage:
  - Processes locked segments using GGUF models for real transcription
  - Streams partials, enqueues confirmed text
  - Falls back to stub mode when GGUF model fails to load
  - Thermal mode affects resampling (16kHz → 8kHz)
- LLM Stage:
  - Context window management (normal/throttle), sliding history
  - Emits partial tokens at punctuation for cascade truncation
  - Integrates GGUF models for real translation with chat template prompts
  - Maintains conversation context across multiple segments
- TTS Stage:
  - Synthesizes streaming audio chunks, reports start/complete events
  - SLA monitoring for TTFA

```mermaid
sequenceDiagram
participant SEG as "SentenceSegmenter"
participant ASR as "ASR Stage"
participant GGUF as "GGUF Inference"
participant LLM as "LLM Stage"
participant TTS as "TTS Stage"
participant NP as "NativePort"
SEG->>ASR : LockedSegment
ASR->>GGUF : gguf_inference_generate(transcription prompt)
GGUF-->>ASR : transcription tokens
ASR->>NP : MSG_ASR_PARTIAL
ASR->>LLM : AsrToLlmElement
LLM->>GGUF : gguf_inference_generate(translation prompt)
GGUF-->>LLM : translation tokens
LLM->>NP : MSG_TRANSLATION_STREAM
LLM->>TTS : LlmToTtsElement
TTS->>NP : MSG_TTS_STARTED
TTS-->>NP : MSG_TTS_COMPLETE
```

**Diagram sources**
- [sentence_segmenter.h:1-142](file://native/include/sentence_segmenter.h#L1-L142)
- [asr_stage.h:1-104](file://native/include/asr_stage.h#L1-L104)
- [asr_stage.cpp:130-206](file://native/src/asr_stage.cpp#L130-L206)
- [llm_stage.h:1-93](file://native/include/llm_stage.h#L1-L93)
- [llm_stage.cpp:178-248](file://native/src/llm_stage.cpp#L178-L248)
- [tts_stage.h:1-79](file://native/include/tts_stage.h#L1-L79)
- [native_port.h:1-179](file://native/include/native_port.h#L1-L179)
- [gguf_inference.h:1-98](file://native/include/gguf_inference.h#L1-98)

**Section sources**
- [sentence_segmenter.h:1-142](file://native/include/sentence_segmenter.h#L1-L142)
- [asr_stage.h:1-104](file://native/include/asr_stage.h#L1-L104)
- [asr_stage.cpp:130-206](file://native/src/asr_stage.cpp#L130-L206)
- [llm_stage.h:1-93](file://native/include/llm_stage.h#L1-L93)
- [llm_stage.cpp:178-248](file://native/src/llm_stage.cpp#L178-L248)
- [tts_stage.h:1-79](file://native/include/tts_stage.h#L1-L79)

## Dependency Analysis
- FFI Bridge depends on EngineManager and NativePort
- EngineManager depends on ModelLoader, PipelineController, and GGUF Inference Backend
- PipelineController composes all stages and primitives
- Stages depend on HAL accelerator, GGUF Inference Backend, and NativePort for messaging
- GGUF Inference Backend depends on llama.cpp libraries and EchoTypes for logging
- NativePort depends on EchoTypes for message tags and payloads

```mermaid
graph LR
FFI["ffi_bridge.cpp"] --> EM["engine_manager.cpp"]
EM --> PC["pipeline_controller.cpp"]
EM --> GGUF["gguf_inference.cpp"]
PC --> RB["audio_ring_buffer.h"]
PC --> Q["bounded_spsc_queue.h"]
PC --> ASR["asr_stage.h"]
PC --> LLM["llm_stage.h"]
PC --> TTS["tts_stage.h"]
PC --> SEG["sentence_segmenter.h"]
ASR --> NP["native_port.cpp"]
ASR --> GGUF
LLM --> NP
LLM --> GGUF
TTS --> NP
NP --> ET["echo_types.h"]
GGUF --> LLAMA["llama.cpp"]
```

**Diagram sources**
- [ffi_bridge.cpp:1-124](file://native/src/ffi_bridge.cpp#L1-L124)
- [engine_manager.cpp:1-202](file://native/src/engine_manager.cpp#L1-L202)
- [pipeline_controller.cpp:1-488](file://native/src/pipeline_controller.cpp#L1-L488)
- [audio_ring_buffer.h:1-192](file://native/include/audio_ring_buffer.h#L1-L192)
- [bounded_spsc_queue.h:1-145](file://native/include/bounded_spsc_queue.h#L1-L145)
- [asr_stage.h:1-104](file://native/include/asr_stage.h#L1-L104)
- [asr_stage.cpp:130-206](file://native/src/asr_stage.cpp#L130-L206)
- [llm_stage.h:1-93](file://native/include/llm_stage.h#L1-L93)
- [llm_stage.cpp:178-248](file://native/src/llm_stage.cpp#L178-L248)
- [tts_stage.h:1-79](file://native/include/tts_stage.h#L1-L79)
- [sentence_segmenter.h:1-142](file://native/include/sentence_segmenter.h#L1-L142)
- [native_port.cpp:1-320](file://native/src/native_port.cpp#L1-L320)
- [echo_types.h:1-136](file://native/include/echo_types.h#L1-L136)
- [gguf_inference.cpp:1-346](file://native/src/gguf_inference.cpp#L1-346)

**Section sources**
- [ffi_bridge.cpp:1-124](file://native/src/ffi_bridge.cpp#L1-L124)
- [engine_manager.cpp:1-202](file://native/src/engine_manager.cpp#L1-L202)
- [pipeline_controller.cpp:1-488](file://native/src/pipeline_controller.cpp#L1-L488)
- [native_port.cpp:1-320](file://native/src/native_port.cpp#L1-L320)
- [echo_types.h:1-136](file://native/include/echo_types.h#L1-L136)
- [gguf_inference.cpp:1-346](file://native/src/gguf_inference.cpp#L1-346)

## Performance Considerations
- Lock-free design:
  - AudioRingBuffer and BoundedSPSCQueue avoid contention and block-free operation
  - Cache-line alignment reduces false sharing
- Cascade truncation:
  - Early downstream activation reduces end-to-end latency
- Threading model:
  - Each stage runs on its own thread; monitors operate independently
- **New**: GGUF Optimization:
  - Memory-mapped model loading for efficient mobile memory usage
  - Configurable context windows capped at 2048 tokens for mobile devices
  - Greedy sampling for deterministic output suitable for translation/ASR
  - Optimized batch processing with 512-token batch sizes
  - Automatic thread count detection with 4-thread default
- SLAs:
  - ASR first-character ≤200ms
  - LLM first-token ≤450ms, throughput ≥35 tokens/sec
  - TTS TTFA ≤100ms
  - E2E budgets: normal ≤800ms, throttle ≤1200ms
- Resource limits:
  - Ring buffer capacity ~65.5s at 16kHz
  - Graceful stop within 2 seconds
- Memory management:
  - Explicit destroy sequences and RAII-like patterns for C++ members
  - Safe NULL handling and placement-new for raw allocations
  - Proper GGUF context cleanup and backend lifecycle management

## Troubleshooting Guide
Common issues and strategies:
- No port registered:
  - Ensure RegisterEchoMessagePort is called before starting pipeline
  - Verify native_port_is_registered returns true
- Session conflicts:
  - Starting pipeline while already running returns session active error
  - Stop pipeline before restart
- Unsupported languages:
  - Validate ISO 639-1 codes against supported list
- **New**: GGUF Model Issues:
  - Check GGUF model file paths and permissions
  - Verify llama.cpp library linking in build configuration
  - Monitor GGUF backend initialization logs for errors
  - Fall back to stub mode when GGUF models fail to load
- Thermal and memory pressure:
  - Monitor thermal state and memory warning messages
  - Critical memory may trigger automatic pipeline stop
- Latency violations:
  - Inspect latency warning messages for specific stages
  - Adjust GGUF context windows and thread counts for performance tuning
- Debugging techniques:
  - Use NativePort messages to trace pipeline events
  - Check EngineManager state transitions and session flags
  - Validate ring buffer size and queue sizes during load spikes
  - Monitor GGUF inference logs for model loading and generation status

**Section sources**
- [native_port.h:1-179](file://native/include/native_port.h#L1-L179)
- [native_port.cpp:1-320](file://native/src/native_port.cpp#L1-L320)
- [engine_manager.h:1-104](file://native/include/engine_manager.h#L1-L104)
- [engine_manager.cpp:65-240](file://native/src/engine_manager.cpp#L65-L240)
- [pipeline_controller.h:1-107](file://native/include/pipeline_controller.h#L1-L107)
- [pipeline_controller.cpp:1-488](file://native/src/pipeline_controller.cpp#L1-L488)
- [echo_types.h:1-136](file://native/include/echo_types.h#L1-L136)
- [gguf_inference.h:1-98](file://native/include/gguf_inference.h#L1-98)
- [gguf_inference.cpp:1-346](file://native/src/gguf_inference.cpp#L1-346)

## Conclusion
QwenEcho's native engine core implements a robust, high-performance audio processing pipeline using lock-free primitives, staged inference, and asynchronous messaging. The EngineManager enforces a clear lifecycle, while PipelineController orchestrates overlapping execution for low-latency streaming. **The new GGUF inference backend provides real AI model execution capabilities through llama.cpp integration**, enabling both ASR transcription and LLM translation with configurable context windows and thread counts. NativePort enables reliable Dart-to-native communication, and EchoTypes standardize contracts across modules. With careful attention to performance, memory management, and error handling, the system scales across platforms and supports extensibility for new models and stages.

## Appendices

### Extending the Pipeline with Custom Stages
- Add a new stage header and implementation following existing stage interfaces
- Integrate with BoundedSPSCQueue for input/output
- Use NativePort to emit typed messages
- Wire the stage in PipelineController's create/start/stop flows
- Update language validation and configuration if needed
- **New**: Optionally integrate GGUF inference for AI-powered processing

### Integrating New AI Models
- Provide new GGUF model paths via EngineManager initialization
- **Updated**: GGUF models are automatically loaded and managed by the inference backend
- Configure context windows and thread counts based on model requirements
- Ensure proper model format compatibility with llama.cpp
- Test both batch and streaming inference modes
- Validate SLAs and update latency tracking thresholds accordingly
- **New**: Implement fallback mechanisms for when GGUF models fail to load

### GGUF Model Development Guidelines
- Use GGUF format compatible with llama.cpp
- Optimize models for mobile deployment (context window ≤ 2048 tokens)
- Consider greedy sampling for deterministic output in translation/ASR tasks
- Test model performance across different device capabilities
- Monitor memory usage and adjust context windows accordingly
- Implement proper error handling for model loading failures