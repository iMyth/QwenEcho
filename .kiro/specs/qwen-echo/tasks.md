# Implementation Plan: QwenEcho

## Overview

This plan implements the QwenEcho on-device simultaneous interpretation engine in C/C++ with a Flutter UI shell connected via Dart FFI. Tasks are ordered to build foundational components first (data structures, HAL, FFI bridge), then pipeline stages, then monitors, and finally the Flutter UI. RapidCheck property-based tests validate correctness properties defined in the design.

## Tasks

- [x] 1. Project structure and core data types
  - [x] 1.1 Create C/C++ project structure and build system
    - Create directory layout: `native/src/`, `native/include/`, `native/tests/`, `native/hal/`
    - Set up CMakeLists.txt with C++17, arm64 targets, and RapidCheck integration
    - Define shared header `echo_types.h` with EngineState enum, MessageType enum, EchoErrorCode enum, EngineConfig struct, AsrToLlmElement struct, LlmToTtsElement struct
    - _Requirements: 1.1, 14.2, 15.1–15.4_

  - [x] 1.2 Implement AudioRingBuffer (SPSC lock-free circular buffer)
    - Implement `AudioRingBuffer` class with power-of-two capacity (2^20 = 1,048,576 samples)
    - Use `std::atomic<uint32_t>` for head/tail with acquire/release ordering
    - Align head and tail on separate 64-byte cache lines to avoid false sharing
    - Implement `write()`, `read()`, `available()`, `advance_read_on_overflow()`
    - Overflow policy: overwrite oldest samples, advance read pointer
    - _Requirements: 3.4, 3.5, 14.1, 14.5, 14.6_

  - [x]* 1.3 Write property tests for AudioRingBuffer
    - **Property 1: Ring Buffer Overflow Preserves Most Recent Data**
    - Generate random write sequences exceeding capacity, verify last C samples retained
    - **Property 2: Ring Buffer SPSC Integrity**
    - Generate concurrent read/write patterns on separate threads, verify no corruption
    - **Validates: Requirements 3.4, 3.5, 14.1, 14.5, 14.6**

  - [x] 1.4 Implement BoundedSPSCQueue template
    - Implement `BoundedSPSCQueue<T, 64>` with slot-based sequence numbers
    - Static assert capacity is power of two for bitmask indexing
    - Implement `try_push()` that drops oldest on overflow, `try_pop()`, `size()`
    - Align head/tail on 64-byte cache lines
    - _Requirements: 14.2, 14.4, 14.5_

  - [x]* 1.5 Write property test for BoundedSPSCQueue
    - **Property 3: Bounded Queue Capacity Enforcement**
    - Generate random push sequences exceeding 64, verify size never exceeds 64
    - Verify oldest element discarded on overflow, push never blocks
    - **Validates: Requirements 14.2, 14.4**

- [x] 2. Platform Abstraction Layer (HAL)
  - [x] 2.1 Define HAL interfaces
    - Create `hal/hal_accelerator.h` with AcceleratorContext, load_model, infer, destroy
    - Create `hal/hal_thermal.h` with get_temperature, register_callback
    - Create `hal/hal_memory.h` with get_rss, get_platform_limit
    - Create `hal/hal_audio.h` with AudioCapture create/start/stop/destroy
    - Create `hal/hal_thread.h` with set_realtime_priority
    - _Requirements: 11.4, 11.5, 11.6, 11.7_

  - [x] 2.2 Implement Android HAL backends
    - Implement NNAPI/Vulkan accelerator backend with CPU fallback
    - Implement AAudio low-latency audio capture/output
    - Implement AThermal temperature polling
    - Implement `/proc/self/statm` memory reading
    - Implement `pthread_setschedparam(SCHED_FIFO)` for RT priority
    - Compile with `-Wl,-z,max-page-size=16384` for Android 15+ compatibility
    - _Requirements: 11.1, 11.2, 11.4, 11.5, 11.7_

  - [x] 2.3 Implement iOS HAL backends
    - Implement CoreML/Metal accelerator backend
    - Implement AVAudioEngine input tap and output
    - Implement ProcessInfo.thermalState polling
    - Implement task_info TASK_VM_INFO memory reading
    - Implement pthread QoS real-time thread priority
    - _Requirements: 11.3, 11.6, 11.7_

- [x] 3. Checkpoint - Core infrastructure
  - Ensure all tests pass, ask the user if questions arise.

- [x] 4. Model Loader and Engine Manager
  - [x] 4.1 Implement Model Loader
    - Validate GGUF header magic bytes (`0x46475547`) and INT4 quantization format
    - Memory-map model files via `mmap` for OS page cache leverage
    - Instantiate ggml inference contexts for ASR, LLM, TTS independently
    - Report per-model memory consumption
    - Return categorized errors: missing, permission denied, invalid format
    - _Requirements: 1.1, 1.2, 1.3, 1.6, 16.1, 16.5_

  - [x]* 4.2 Write property test for Model Validation
    - **Property 20: Model Validation Error Reporting**
    - Generate random invalid file scenarios (missing, corrupted magic, wrong quant type)
    - Verify error contains model name, file path, and failure reason category
    - **Validates: Requirements 1.3, 16.5**

  - [x] 4.3 Implement Engine Manager state machine and lifecycle
    - Implement state transitions: Uninitialized → Initializing → Ready → Running → Stopping → Ready
    - Implement `engine_manager_create`, `load_models`, `start_pipeline`, `stop_pipeline`, `destroy`
    - Guard StartEchoPipeline against non-Ready states
    - Guard against duplicate Init calls (return ECHO_ERR_ALREADY_INIT)
    - Guard against duplicate StartEchoPipeline (return ECHO_ERR_SESSION_ACTIVE)
    - Handle StopEchoPipeline when no session (return ECHO_OK)
    - _Requirements: 1.5, 2.1, 2.2, 2.4, 2.6, 2.7_

  - [x]* 4.4 Write property tests for Engine State Machine and Init Idempotence
    - **Property 4: Engine State Machine Valid Transitions**
    - Generate random sequences of Init/Start/Stop/Register calls, verify state constraints
    - **Property 21: Engine Init Idempotence**
    - Generate repeated Init calls (1–100 times) after success, verify ECHO_ERR_ALREADY_INIT
    - **Validates: Requirements 1.5, 2.4, 2.5, 2.6, 2.7**

- [x] 5. Dart FFI Bridge
  - [x] 5.1 Implement FFI Bridge C-linkage functions
    - Implement `InitQwenEchoEngine(asr_path, llm_path, tts_path)` → int32_t
    - Implement `StartEchoPipeline(source_lang, target_lang)` → int32_t
    - Implement `StopEchoPipeline()` → int32_t
    - Implement `RegisterEchoMessagePort(dart_port_id)` → int32_t
    - All with `extern "C"` linkage and `__attribute__((visibility("default")))`
    - Return 0 success, negative EchoErrorCode on failure
    - _Requirements: 15.1, 15.2, 15.3, 15.4, 15.6_

  - [x] 5.2 Implement Native Port message dispatch
    - Implement `Dart_PostCObject` message serialization for all MessageType values
    - Serialize payloads: ASR-partial, ASR-confirmed, translation-stream, translation-done, TTS-started, TTS-complete, error, thermal-state, memory-warning, latency-warning, sample-drop
    - Implement port registration replacement (new replaces old)
    - Enforce port prerequisite: StartEchoPipeline/StopEchoPipeline return ECHO_ERR_NO_PORT if no port registered
    - _Requirements: 15.5, 15.7, 15.8_

  - [x]* 5.3 Write property tests for FFI Bridge
    - **Property 17: Native Port Message Format Completeness**
    - Generate random pipeline events, verify type tag and required fields present
    - **Property 18: Port Registration Replacement**
    - Generate random sequences of port registrations, verify only latest receives messages
    - **Property 19: Port Prerequisite for Pipeline Operations**
    - Generate random call sequences without prior registration, verify ECHO_ERR_NO_PORT
    - **Validates: Requirements 15.5, 15.7, 15.8**

- [x] 6. Checkpoint - Engine lifecycle and FFI
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Audio Collector and VAD/Sentence Segmenter
  - [x] 7.1 Implement Audio Collector thread
    - Configure platform audio input via HAL (16kHz, 16-bit, mono)
    - Run on real-time thread priority via HAL (SCHED_FIFO / RT QoS)
    - Write captured audio continuously to Ring Buffer
    - Detect sample drops exceeding 160 samples (10ms), send MSG_SAMPLE_DROP
    - Produce first samples in Ring Buffer within 50ms of pipeline start
    - _Requirements: 3.1, 3.2, 3.3, 3.6, 3.7_

  - [x]* 7.2 Write property test for Audio Sample Drop Reporting
    - **Property 15: Audio Sample Drop Reporting**
    - Generate random drop sizes (1–1000 samples), verify MSG_SAMPLE_DROP only for >160
    - **Validates: Requirements 3.7**

  - [x] 7.3 Implement VAD + Sentence Segmenter
    - Integrate FunASR-Nano's FSMN-VAD for per-frame speech/non-speech classification (≤30ms)
    - Implement state machine: Idle → Accumulating → Locking → Idle
    - Lock on 400ms silence after ≥200ms speech
    - Lock on sentence-ending punctuation detection in ASR output
    - Force-lock at 15s continuous speech
    - Enforce minimum 200ms speech duration before locking
    - Pass locked segments to ASR stage, immediately begin new segment
    - Support language-appropriate punctuation rules for 52 FunASR languages
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7_

  - [x]* 7.4 Write property test for Sentence Segmenter
    - **Property 9: Sentence Segmenter Lock Conditions**
    - Generate random audio durations + silence patterns + punctuation occurrences
    - Verify locking on any of: 400ms silence, punctuation, 15s force-lock
    - Verify minimum 200ms speech enforced, new segment begins after lock
    - **Validates: Requirements 4.2, 4.3, 4.4, 4.5, 4.7**

- [x] 8. ASR Stage
  - [x] 8.1 Implement ASR processing stage
    - Receive locked audio chunks from Sentence Segmenter
    - Run FunASR-Nano inference via HAL accelerator (NPU-first)
    - Stream partial (unconfirmed) tokens to UI via Native Port (MSG_ASR_PARTIAL)
    - Finalize confirmed text on sentence boundary (MSG_ASR_CONFIRMED)
    - Achieve ≤200ms first-character latency
    - Discard unintelligible/noise-only chunks with silent-discard notification
    - Report SLA violations (>200ms) via MSG_LATENCY_WARNING
    - In Throttle mode, resample from 16kHz to 8kHz before inference
    - Enqueue confirmed text into ASR→LLM bounded queue
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6, 10.5_

  - [x]* 8.2 Write property test for SLA Violation Reporting (ASR)
    - **Property 16: SLA Violation Stage Identification (ASR portion)**
    - Generate random ASR latencies (0–2000ms), verify MSG_LATENCY_WARNING for >200ms
    - Verify delayed result still delivered
    - **Validates: Requirements 5.6, 8.5**

- [x] 9. LLM Translation Stage
  - [x] 9.1 Implement LLM translation stage
    - Dequeue confirmed text from ASR→LLM bounded queue
    - Maintain sliding context window: last 3 confirmed translations prepended
    - Run Qwen3-4B-Instruct inference via HAL accelerator (NPU-first)
    - Stream output tokens to UI (MSG_TRANSLATION_STREAM) and enqueue to LLM→TTS queue
    - Send MSG_TRANSLATION_DONE on segment completion
    - Achieve ≤450ms first-token latency, ≥35 tokens/second throughput
    - Cascade truncation: emit partial results at punctuation boundaries for TTS
    - Use 512-token context window in Normal mode, 256 in Throttle mode
    - Complete current translation with original window on mid-translation mode change
    - Truncate oldest context entries first when combined input exceeds window
    - Report SLA violations (>450ms) via MSG_LATENCY_WARNING
    - _Requirements: 6.1, 6.2, 6.3, 6.4, 6.5, 6.6, 6.7, 6.8, 6.9, 8.2_

  - [x]* 9.2 Write property tests for LLM Translation
    - **Property 6: LLM Context Window Mode Adaptation**
    - Generate random thermal modes + translation requests, verify correct window size
    - Verify mid-translation mode change completes with original window
    - **Property 7: LLM Context Truncation**
    - Generate random context histories exceeding window, verify oldest truncated first
    - Verify current segment never truncated
    - **Property 8: LLM Sliding Context Window**
    - Generate sequences of 1–20 confirmed translations
    - Verify exactly last 3 translations prepended (or all available if <3)
    - **Validates: Requirements 6.2, 6.4, 6.5, 6.8, 6.9**

- [x] 10. TTS Synthesis Stage
  - [x] 10.1 Implement TTS synthesis stage
    - Dequeue translated text from LLM→TTS bounded queue
    - Begin synthesis at punctuation boundaries (cascade truncation)
    - Run Qwen3-TTS-Streaming inference via HAL accelerator (NPU-first)
    - Output PCM audio at 24kHz, 16-bit, mono in streaming chunks
    - Achieve ≤100ms TTFA (time to first audio)
    - Send MSG_TTS_STARTED and MSG_TTS_COMPLETE events via Native Port
    - Operate concurrently without blocking ASR/LLM
    - Skip failed segments, log failure, continue to next
    - Discard whitespace-only/punctuation-only text segments (no TTS_STARTED)
    - Report SLA violations (>100ms) via MSG_LATENCY_WARNING
    - Feed audio output to platform speaker via HAL audio output
    - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5, 7.6, 8.4_

  - [x]* 10.2 Write property tests for TTS Stage
    - **Property 10: TTS Whitespace/Punctuation Discard**
    - Generate random strings of whitespace/punctuation combinations
    - Verify zero audio output and no TTS_STARTED event
    - **Property 11: TTS Failure Resilience**
    - Inject random failures at segment K, verify segment K+1 processes normally
    - Verify pipeline not halted or restarted
    - **Validates: Requirements 7.5, 7.6**

- [x] 11. Checkpoint - Pipeline stages complete
  - Ensure all tests pass, ask the user if questions arise.

- [x] 12. Thermal Monitor
  - [x] 12.1 Implement Thermal Monitor
    - Poll hardware temperature every 5 seconds via HAL
    - Implement thermal state machine: Normal → Throttle → Critical with hysteresis
    - Normal → Throttle when temp > 43°C
    - Throttle → Normal when temp ≤ 42°C
    - Throttle → Critical when temp > 50°C
    - Critical → Throttle when temp ≤ 45°C
    - Send MSG_THERMAL_STATE to UI on every transition
    - Pause pipeline on Critical, resume in Throttle when cooled to ≤45°C
    - Route thermal mode changes to Engine Manager for LLM/ASR adaptation
    - Run on low-priority thread
    - _Requirements: 10.1, 10.2, 10.3, 10.4, 10.5, 10.6, 10.7, 10.8, 10.9_

  - [x]* 12.2 Write property test for Thermal State Machine
    - **Property 5: Thermal State Machine Transitions**
    - Generate random temperature sequences (30°C–60°C)
    - Verify correct state transitions and MSG_THERMAL_STATE notification on each
    - Verify hysteresis: no oscillation at boundary temperatures
    - **Validates: Requirements 10.2, 10.3, 10.6, 10.7, 10.8, 10.9**

- [x] 13. Memory Monitor
  - [x] 13.1 Implement Memory Monitor
    - Sample process RSS every 2 seconds via HAL
    - Implement two-level mitigation:
      - Level 1 at 85%: release LLM KV caches + TTS output buffers
      - Level 2 at 95%: graceful pipeline stop + MSG_MEMORY_WARNING to UI
    - Platform limits: 2.5GB Android, 2.0GB iOS (from HAL)
    - Run on low-priority thread
    - _Requirements: 9.1, 9.2, 9.3, 9.4, 9.5_

  - [x]* 13.2 Write property test for Memory Mitigation
    - **Property 12: Memory Mitigation Two-Level Response**
    - Generate random memory readings (0%–100% of limit)
    - Verify Level 1 action at >85%, Level 2 action at >95% after Level 1 applied
    - Verify pipeline stop and MSG_MEMORY_WARNING on Level 2
    - **Validates: Requirements 9.4, 9.5**

- [x] 14. Pipeline Stop Segment Handling
  - [x] 14.1 Implement graceful pipeline stop logic
    - On StopEchoPipeline: process all segments already locked by Sentence Segmenter through the full pipeline
    - Discard unlocked audio remaining in Ring Buffer
    - Release pipeline resources (threads, queues) within 2 seconds
    - Validate language pair support on StartEchoPipeline (return ECHO_ERR_UNSUPPORTED_LANG)
    - _Requirements: 2.1, 2.2, 2.5_

  - [x]* 14.2 Write property test for Pipeline Stop Segment Handling
    - **Property 22: Pipeline Stop Segment Handling**
    - Generate random pipeline states with N locked + M unlocked segments
    - Verify all N locked segments processed, all M unlocked segments discarded
    - **Validates: Requirements 2.2**

- [x] 15. Checkpoint - Engine complete
  - Ensure all tests pass, ask the user if questions arise.

- [x] 16. Flutter UI Shell
  - [x] 16.1 Implement Dart FFI bindings and Port Manager
    - Create Dart FFI bindings for the 4 C-linkage functions using `dart:ffi`
    - Implement Port Manager: register Native Port, handle incoming messages
    - Deserialize Native Port messages into typed Dart objects by MessageType
    - Wire init/start/stop lifecycle to UI controls
    - _Requirements: 15.1, 15.2, 15.3, 15.4, 15.5_

  - [x] 16.2 Implement Split View Controller with bilateral display
    - Build top-bottom 50/50 split layout with top half rotated 180 degrees
    - Lock screen orientation to portrait mode
    - Each half displays independently: own ASR text + incoming translation
    - Support full-duplex bilateral operation (both halves simultaneous)
    - Display idle indicator when no text received for a half
    - _Requirements: 12.1, 12.2, 12.8_

  - [x] 16.3 Implement text rendering with three-color state and scrolling
    - Render MSG_ASR_PARTIAL text in gray (#9E9E9E) in originating speaker's half
    - Render MSG_ASR_CONFIRMED text in white (#FFFFFF) in originating speaker's half
    - Render MSG_TRANSLATION_STREAM tokens in green (#00E676) in opposing half with typewriter effect
    - Auto-scroll to most recent text, retain max 50 lines per half
    - Discard oldest lines when exceeding 50-line limit
    - _Requirements: 12.3, 12.4, 12.5, 12.7_

  - [x]* 16.4 Write property tests for UI text color and line buffer
    - **Property 13: UI Text Color Mapping**
    - Generate random message types, verify correct color assignment
    - **Property 14: UI Line Buffer Limit**
    - Generate random line counts (1–200), verify max 50 retained, oldest discarded
    - **Validates: Requirements 12.3, 12.4, 12.5, 12.7**

  - [x] 16.5 Implement offline status indicator and error display
    - Display persistent offline-status indicator on main interpretation screen
    - Display thermal mode notifications from MSG_THERMAL_STATE
    - Display memory warnings and latency warnings from respective message types
    - Zero AI logic in UI Shell — all processing delegated to Engine
    - _Requirements: 12.6, 13.3_

- [x] 17. Offline and Security Enforcement
  - [x] 17.1 Implement offline-only enforcement
    - Verify Engine makes zero network requests after model provisioning
    - Store model files within application sandbox (no external access)
    - Ensure no telemetry, analytics, crash-reporting, or update-check code included
    - Verify launch and operation without network interfaces present
    - _Requirements: 13.1, 13.2, 13.4, 13.5, 13.6_

- [x] 18. End-to-end latency validation and wiring
  - [x] 18.1 Wire full pipeline with cascade truncation
    - Connect Audio Collector → Ring Buffer → VAD/Segmenter → ASR → LLM → TTS → Audio Output
    - Implement cascade truncation: downstream stages begin before upstream completes
    - Ensure E2E latency budget: ASR ≤200ms + LLM ≤450ms + TTS ≤100ms = ≤800ms (Normal)
    - Ensure Throttle mode E2E ≤1200ms
    - Report MSG_LATENCY_WARNING when E2E exceeds threshold for current thermal mode
    - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.5_

  - [x]* 18.2 Write property test for SLA Violation identification (all stages)
    - **Property 16: SLA Violation Stage Identification (full)**
    - Generate random latencies per stage (0–2000ms)
    - Verify correct stage identification, actual latency, and budget in MSG_LATENCY_WARNING
    - Verify delayed results still delivered
    - **Validates: Requirements 5.6, 6.7, 8.5**

- [x] 19. Final checkpoint - Full integration
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests use RapidCheck (C++) and fast-check (Dart) as specified in the design
- All 22 correctness properties from the design are covered across property test sub-tasks
- The native engine is the primary implementation target; Flutter UI is a thin shell
- HAL backends allow platform-specific code to be isolated and tested independently
- Cascade truncation is the key design pattern enabling sub-800ms E2E latency

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1", "2.1"] },
    { "id": 1, "tasks": ["1.2", "1.4", "2.2", "2.3"] },
    { "id": 2, "tasks": ["1.3", "1.5"] },
    { "id": 3, "tasks": ["4.1", "5.1"] },
    { "id": 4, "tasks": ["4.2", "4.3", "5.2"] },
    { "id": 5, "tasks": ["4.4", "5.3"] },
    { "id": 6, "tasks": ["7.1", "7.3"] },
    { "id": 7, "tasks": ["7.2", "7.4", "8.1"] },
    { "id": 8, "tasks": ["8.2", "9.1"] },
    { "id": 9, "tasks": ["9.2", "10.1"] },
    { "id": 10, "tasks": ["10.2", "12.1", "13.1"] },
    { "id": 11, "tasks": ["12.2", "13.2", "14.1"] },
    { "id": 12, "tasks": ["14.2", "16.1"] },
    { "id": 13, "tasks": ["16.2", "16.3", "16.5", "17.1"] },
    { "id": 14, "tasks": ["16.4", "18.1"] },
    { "id": 15, "tasks": ["18.2"] }
  ]
}
```
