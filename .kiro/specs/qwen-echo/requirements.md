# Requirements Document

## Introduction

QwenEcho is a 100% on-device, air-gapped AI simultaneous interpretation mobile app. It provides real-time bilateral translation between two speakers in a face-to-face setting, running entirely offline with no cloud dependency. The architecture uses Flutter as a lightweight declarative UI shell and a C/C++ native engine for all heavy audio/AI pipeline work. Communication between layers occurs via Dart FFI async (Native Ports). The system targets Android 11+ and iOS 16+, leveraging phone NPU/GPU hardware acceleration for AI inference with strict performance and thermal constraints.

## Glossary

- **Engine**: The C/C++ native runtime responsible for all audio capture, ASR, LLM translation, and TTS synthesis operations
- **Pipeline**: The end-to-end processing chain from audio capture through ASR, LLM translation, to TTS output
- **ASR**: Automatic Speech Recognition — converts spoken audio into text using the FunASR-Nano model
- **LLM**: Large Language Model — Qwen3-4B-Instruct used for bilingual translation
- **TTS**: Text-to-Speech — Qwen3-TTS-Streaming model that synthesizes audio from translated text
- **Ring_Buffer**: A lock-free circular buffer holding PCM audio data (16kHz, 16-bit, mono)
- **VAD**: Voice Activity Detection — determines when speech is present in the audio stream
- **GGUF**: A quantized model file format used for on-device inference
- **INT4**: 4-bit integer quantization applied to all models to reduce memory footprint
- **NPU**: Neural Processing Unit — dedicated hardware accelerator for AI inference
- **TTFA**: Time To First Audio — latency from translation output to first audible TTS packet
- **Thermal_Monitor**: The subsystem that polls hardware temperature and triggers throttling
- **Dart_FFI_Bridge**: The Foreign Function Interface layer connecting the Flutter UI shell to the C/C++ Engine
- **Sentence_Segmenter**: The component that detects sentence boundaries using silence duration or punctuation
- **Context_Window**: The sliding window of previous translations prepended for translation coherence
- **UI_Shell**: The Flutter-based user interface layer responsible only for rendering and user interaction
- **Split_View**: The face-to-face display mode with top half rotated 180 degrees for the opposing speaker

## Requirements

### Requirement 1: Engine Initialization

**User Story:** As a user, I want the app to initialize all AI models on launch, so that interpretation is available immediately without network access.

#### Acceptance Criteria

1. WHEN the app launches, THE Engine SHALL load the ASR model (FunASR-Nano, ~150MB GGUF/INT4), the LLM model (Qwen3-4B-Instruct, ~2.2GB GGUF/INT4), and the TTS model (Qwen3-TTS-Streaming, ~250MB GGUF/INT4) from local storage within 15 seconds on supported hardware
2. THE Engine SHALL complete all model loading without any network requests
3. IF a model file is missing, unreadable, or fails GGUF header validation, THEN THE Engine SHALL report an error to the UI_Shell via the Dart_FFI_Bridge identifying: the affected model name, the file path attempted, and the failure reason (missing, permission denied, or invalid format)
4. WHEN InitQwenEchoEngine(asr_path, llm_path, tts_path) is called, THE Dart_FFI_Bridge SHALL initialize the Engine with the specified model file paths and send a completion status (success or failure with error details) to the UI_Shell via the registered Native Port when loading finishes
5. IF InitQwenEchoEngine is called while the Engine is already initialized, THEN THE Engine SHALL return an already-initialized status without reloading models
6. IF the device has insufficient available memory to load all models, THEN THE Engine SHALL report a memory allocation failure identifying the model that failed to load and release any partially loaded models

### Requirement 2: Pipeline Lifecycle Management

**User Story:** As a user, I want to start and stop interpretation sessions, so that I can control when the app is actively listening and translating.

#### Acceptance Criteria

1. WHEN StartEchoPipeline(source_lang, target_lang) is called with a supported ISO language pair, THE Engine SHALL begin audio capture and activate the full ASR-LLM-TTS pipeline within 500 milliseconds
2. WHEN StopEchoPipeline() is called, THE Engine SHALL cease audio capture, process any audio already locked by the Sentence_Segmenter through the pipeline, discard unlocked audio remaining in the Ring_Buffer, and release pipeline resources within 2 seconds
3. WHEN RegisterEchoMessagePort(dart_port_id) is called, THE Dart_FFI_Bridge SHALL establish an async communication channel for streaming results to the UI_Shell
4. IF StartEchoPipeline is called before InitQwenEchoEngine completes, THEN THE Engine SHALL return an error indicating the Engine is not ready
5. IF StartEchoPipeline is called with a language pair not supported by the ASR model, THEN THE Engine SHALL return an error identifying the unsupported language code without activating the pipeline
6. IF StartEchoPipeline is called while a pipeline session is already active, THEN THE Engine SHALL return an error indicating a session is already in progress without disrupting the active session
7. IF StopEchoPipeline is called when no pipeline session is active, THEN THE Engine SHALL return successfully with no side effects

### Requirement 3: Audio Capture and Ring Buffer

**User Story:** As a user, I want continuous audio capture during interpretation, so that no spoken words are missed.

#### Acceptance Criteria

1. WHILE the Pipeline is active, THE Engine SHALL capture PCM audio at 16kHz sample rate, 16-bit depth, mono channel
2. WHILE the Pipeline is active, THE Engine SHALL write captured audio to the Ring_Buffer using lock-free operations with no more than 5 milliseconds latency between sample acquisition and buffer availability
3. WHILE the Pipeline is active, THE Engine SHALL run the audio collector thread at real-time scheduling priority (SCHED_FIFO on Android, real-time thread priority on iOS)
4. IF the Ring_Buffer reaches capacity, THEN THE Engine SHALL overwrite the oldest samples without blocking the audio collector thread
5. THE Ring_Buffer SHALL hold at least 30 seconds of PCM audio (960,000 samples at 16kHz, approximately 1.83MB)
6. WHEN StartEchoPipeline is called, THE Engine SHALL begin audio capture and produce the first samples in the Ring_Buffer within 50 milliseconds
7. IF the audio collector thread detects sample drops exceeding 10 milliseconds of audio (160 samples), THEN THE Engine SHALL report the drop event to the UI_Shell via the Dart_FFI_Bridge

### Requirement 4: Voice Activity Detection and Sentence Segmentation

**User Story:** As a user, I want the system to detect when I am speaking and segment my speech into translatable units, so that translation occurs naturally at sentence boundaries.

#### Acceptance Criteria

1. WHILE the Pipeline is active, THE ASR SHALL perform streaming Voice Activity Detection on incoming audio and classify each audio frame as speech or non-speech within 30 milliseconds of frame receipt
2. WHEN the VAD detects a transition from non-speech to speech, THE Sentence_Segmenter SHALL mark the beginning of a new audio segment
3. WHEN 400 milliseconds of continuous silence is detected following speech, THE Sentence_Segmenter SHALL lock the current audio segment for processing, provided the segment contains at least 200 milliseconds of speech audio
4. WHEN sentence-ending punctuation (period, question mark, or exclamation mark) is detected in the ASR output, THE Sentence_Segmenter SHALL lock the current audio segment for processing
5. IF a speaker produces continuous speech exceeding 15 seconds without triggering a silence or punctuation boundary, THEN THE Sentence_Segmenter SHALL force-lock the current audio segment for processing at the 15-second mark and begin a new segment
6. THE Sentence_Segmenter SHALL support the 52 languages provided by the FunASR-Nano model, applying language-appropriate sentence-ending punctuation rules for segmentation
7. WHEN the Sentence_Segmenter locks a segment, THE Sentence_Segmenter SHALL pass the locked segment to the ASR transcription stage and immediately begin accumulating the next segment

### Requirement 5: ASR Processing

**User Story:** As a user, I want my speech converted to text in real-time, so that it can be translated and displayed immediately.

#### Acceptance Criteria

1. WHEN the Sentence_Segmenter locks an audio chunk, THE ASR SHALL begin transcription and emit the first recognized character to the UI_Shell within 200 milliseconds (first-character latency)
2. WHILE transcription is in progress, THE ASR SHALL stream temporary (unconfirmed) text segments to the UI_Shell via the registered Native Port at least once per recognized token or partial word
3. WHEN a sentence boundary is confirmed, THE ASR SHALL finalize the text segment, mark it with a confirmed status indicator, and deliver the complete finalized segment to the UI_Shell within 100 milliseconds of boundary detection
4. THE ASR SHALL process audio using NPU-accelerated inference via NNAPI or Vulkan on Android and CoreML or Metal on iOS
5. IF the ASR cannot produce a transcription result from a locked audio chunk (due to unintelligible audio, noise-only input, or unsupported language), THEN THE ASR SHALL discard the chunk and send a silent-discard notification to the UI_Shell without interrupting the pipeline
6. IF the ASR exceeds the 200-millisecond first-character latency threshold, THEN THE ASR SHALL still emit the result when available and report an SLA-violation event to the Engine for diagnostic logging

### Requirement 6: LLM Translation

**User Story:** As a user, I want my speech translated accurately in real-time, so that the other speaker understands me without delay.

#### Acceptance Criteria

1. WHEN the ASR produces a confirmed text segment, THE LLM SHALL begin translation and produce the first output token within 450 milliseconds
2. THE LLM SHALL prepend the last 3 confirmed translations as sliding context to maintain coherence across segments
3. THE LLM SHALL achieve a throughput of at least 35 tokens per second on NPU-accelerated hardware
4. WHILE in Normal thermal mode, THE LLM SHALL use a 512-token context window
5. WHILE in Throttle thermal mode, THE LLM SHALL reduce to a 256-token context window
6. THE LLM SHALL perform all inference using GGUF/INT4 quantized weights without network access
7. IF the LLM fails to produce the first output token within 450 milliseconds, THEN THE Engine SHALL log an SLA-violation event and continue processing with the delayed result
8. IF the combined sliding context (3 previous translations) plus the current segment exceeds the active context window size, THEN THE LLM SHALL truncate the oldest context entries first to fit within the window limit
9. IF the Engine transitions from Normal to Throttle mode while the LLM is mid-translation, THEN THE LLM SHALL complete the current segment using the existing context window and apply the reduced 256-token window starting with the next segment

### Requirement 7: TTS Synthesis

**User Story:** As a user, I want translated text spoken aloud in real-time, so that the other speaker can hear the translation while I continue talking.

#### Acceptance Criteria

1. WHEN the LLM produces text up to a punctuation boundary (period, comma, or semicolon), THE TTS SHALL begin audio synthesis (cascade truncation) and produce the first audio packet within 100 milliseconds (TTFA)
2. WHILE the Pipeline is active, THE TTS SHALL stream PCM audio output (24kHz, 16-bit, mono) incrementally, producing one audio chunk per punctuation-delimited segment as translation tokens arrive
3. THE TTS SHALL perform synthesis using GGUF/INT4 quantized weights on NPU-accelerated hardware
4. WHILE the Pipeline is active, THE TTS SHALL operate concurrently with ASR and LLM without blocking or interrupting audio capture or translation processing (translate-while-speaking)
5. IF TTS synthesis fails or produces no audio for a given text segment, THEN THE TTS SHALL skip the failed segment, log the failure, and continue processing subsequent segments without halting the Pipeline
6. IF the LLM produces a text segment containing only whitespace or punctuation with no translatable content, THEN THE TTS SHALL discard the segment without producing audio output

### Requirement 8: End-to-End Latency

**User Story:** As a user, I want the total delay from my speech to hearing the translation to be imperceptible, so that conversation flows naturally.

#### Acceptance Criteria

1. THE Pipeline SHALL maintain an end-to-end latency of 800 milliseconds or less, measured from the moment the Sentence_Segmenter locks an audio chunk to the moment the first TTS audio packet begins playback
2. WHILE in Normal thermal mode, THE Engine SHALL stream ASR output to the LLM and LLM output to the TTS using cascade truncation at punctuation boundaries, so that downstream stages begin processing before upstream stages complete
3. WHILE in Throttle thermal mode, THE Pipeline SHALL maintain an end-to-end latency of 1200 milliseconds or less, measured from the moment the Sentence_Segmenter locks an audio chunk to the moment the first TTS audio packet begins playback
4. THE Pipeline SHALL allocate its latency budget as: ASR first-character latency no more than 200 milliseconds, LLM first-token latency no more than 450 milliseconds, and TTS time-to-first-audio no more than 100 milliseconds
5. IF the end-to-end latency exceeds the applicable threshold for the current thermal mode, THEN THE Engine SHALL report a latency warning to the UI_Shell via the Dart_FFI_Bridge indicating which stage exceeded its budget

### Requirement 9: Memory Management

**User Story:** As a user, I want the app to run smoothly without being killed by the OS, so that interpretation sessions are uninterrupted.

#### Acceptance Criteria

1. WHILE running on Android, THE Engine SHALL consume no more than 2.5GB of RAM across all loaded models and runtime buffers
2. WHILE running on iOS, THE Engine SHALL consume no more than 2.0GB of RAM across all loaded models and runtime buffers
3. WHILE the Pipeline is active, THE Engine SHALL sample process memory usage at least once every 2 seconds
4. IF memory usage exceeds 85% of the platform limit (2.125GB on Android, 1.7GB on iOS), THEN THE Engine SHALL release LLM KV caches and TTS audio output buffers to reduce consumption below the threshold
5. IF memory usage remains above 95% of the platform limit after cache release, THEN THE Engine SHALL stop the Pipeline gracefully and report a memory pressure error to the UI_Shell via the Dart_FFI_Bridge

### Requirement 10: Thermal Mitigation

**User Story:** As a user, I want the app to avoid overheating my phone, so that I can use it for extended interpretation sessions.

#### Acceptance Criteria

1. WHILE the Pipeline is active, THE Thermal_Monitor SHALL poll hardware temperature every 5 seconds
2. WHILE hardware temperature is at or below 42 degrees Celsius, THE Engine SHALL operate in Normal mode with full performance specifications as defined in Requirements 6, 7, and 8
3. WHEN hardware temperature exceeds 43 degrees Celsius, THE Engine SHALL transition to Throttle mode
4. WHILE in Throttle mode, THE Engine SHALL reduce the LLM context window from 512 to 256 tokens
5. WHILE in Throttle mode, THE Engine SHALL lower the ASR sample rate from 16kHz to 8kHz
6. WHEN hardware temperature returns to 42 degrees Celsius or below after being in Throttle mode, THE Engine SHALL transition back to Normal mode
7. IF hardware temperature exceeds 50 degrees Celsius while in Throttle mode, THEN THE Engine SHALL pause the Pipeline and notify the UI_Shell that interpretation is suspended due to critical temperature
8. WHEN the Engine transitions between Normal mode and Throttle mode, THE Engine SHALL notify the UI_Shell of the current thermal mode via the Dart_FFI_Bridge
9. WHEN the Pipeline is paused due to critical temperature AND hardware temperature returns to 45 degrees Celsius or below, THE Engine SHALL resume the Pipeline in Throttle mode

### Requirement 11: Platform Compatibility

**User Story:** As a developer, I want the app to support modern Android and iOS platforms, so that it reaches the target user base.

#### Acceptance Criteria

1. THE Engine SHALL compile, install, and execute the full ASR-LLM-TTS pipeline on Android 11 (API level 30) and above
2. THE Engine SHALL compile native .so libraries with 16KB page alignment (linker flag -Wl,-z,max-page-size=16384) for Android 15+ compatibility, while remaining loadable on Android 11-14 devices with 4KB page size
3. THE Engine SHALL compile, install, and execute the full ASR-LLM-TTS pipeline on iOS 16 and above
4. WHILE running on Android, THE Engine SHALL use NNAPI or Vulkan for NPU/GPU acceleration
5. IF neither NNAPI nor Vulkan is available on the Android device, THEN THE Engine SHALL fall back to CPU-based inference and report the degraded acceleration status to the UI_Shell via the Dart_FFI_Bridge
6. WHILE running on iOS, THE Engine SHALL use CoreML for NPU acceleration and Metal for GPU compute
7. THE Engine SHALL target arm64-v8a as the sole Android ABI and arm64 as the sole iOS architecture

### Requirement 12: Bilateral Interpretation UI

**User Story:** As a user, I want a face-to-face split-screen view, so that both speakers can read text and hear translations from their own perspective.

#### Acceptance Criteria

1. THE UI_Shell SHALL display a top-bottom split view divided equally (50/50) with the top half rotated 180 degrees to face the opposing speaker, and SHALL lock the screen orientation to portrait mode to prevent layout disruption
2. THE UI_Shell SHALL support full-duplex bilateral interpretation where both halves operate independently and simultaneously, each half displaying its own speaker's ASR text and the incoming translation from the opposing speaker's stream
3. WHILE ASR text is temporary (unconfirmed), THE UI_Shell SHALL render the text in light gray (#9E9E9E) in the originating speaker's half of the split view
4. WHEN ASR text is confirmed (finalized with punctuation), THE UI_Shell SHALL render the text in white (#FFFFFF) in the originating speaker's half of the split view
5. WHEN translation output is streaming, THE UI_Shell SHALL render the translated text in bright green (#00E676) in the opposing speaker's half, appending each token as it arrives from the Engine to produce a progressive typewriter effect
6. THE UI_Shell SHALL contain zero AI logic and delegate all processing to the Engine via the Dart_FFI_Bridge
7. WHEN confirmed text or translation text exceeds the visible area of a half, THE UI_Shell SHALL automatically scroll to keep the most recent text visible and SHALL retain a maximum of 50 displayed lines of history per half, discarding the oldest lines when the limit is exceeded
8. IF no ASR or translation text has been received for a half, THEN THE UI_Shell SHALL display an idle indicator showing the half is awaiting speech input

### Requirement 13: Offline Operation

**User Story:** As a user, I want the app to function in air-gapped environments, so that I can use it in locations without internet connectivity.

#### Acceptance Criteria

1. THE Engine SHALL perform all ASR, LLM, and TTS operations without any network connectivity
2. THE Engine SHALL store all model files (ASR ~150MB, LLM ~2.2GB, TTS ~250MB) within the application sandbox on-device, inaccessible to other applications
3. THE UI_Shell SHALL display a persistent offline-status indicator visible on the main interpretation screen at all times while the app is in the foreground
4. THE Engine SHALL make zero network requests at any point after initial model provisioning is complete, regardless of whether an interpretation session is active or inactive
5. THE Engine SHALL not include or invoke any telemetry, analytics, crash-reporting, or update-check functionality that transmits data off-device
6. IF no network interface is present on the device, THEN THE Engine SHALL launch and operate without error or degraded functionality

### Requirement 14: Concurrency and Thread Safety

**User Story:** As a developer, I want lock-free concurrent processing, so that the pipeline achieves real-time performance without thread contention.

#### Acceptance Criteria

1. THE Ring_Buffer SHALL use lock-free data structures for concurrent read and write access between the audio collector thread and the ASR consumer thread
2. THE Engine SHALL isolate ASR, LLM, and TTS into separate processing stages that communicate via bounded lock-free queues with a maximum capacity of 64 elements per queue
3. THE Engine SHALL assign real-time scheduling priority to the audio collector thread using the highest available real-time priority class on the platform (SCHED_FIFO on Android, real-time thread QoS on iOS)
4. IF a downstream queue (ASR-to-LLM or LLM-to-TTS) reaches capacity, THEN THE Engine SHALL discard the oldest enqueued element to make room for the new element without blocking the audio collector thread
5. THE Engine SHALL guarantee that all lock-free queue operations produce complete, non-corrupted data elements such that no consumer thread reads a partially written element
6. IF the Ring_Buffer reaches capacity, THEN THE Engine SHALL overwrite the oldest audio samples and advance the read pointer so that the ASR consumer always reads the most recent available audio data

### Requirement 15: Dart FFI Bridge Interface

**User Story:** As a developer, I want a well-defined FFI interface between Flutter and the native engine, so that the UI layer remains decoupled from AI processing.

#### Acceptance Criteria

1. THE Dart_FFI_Bridge SHALL expose InitQwenEchoEngine(asr_path, llm_path, tts_path) as a C-linkage function with default symbol visibility that returns a signed 32-bit integer status code (0 for success, negative value for failure)
2. THE Dart_FFI_Bridge SHALL expose StartEchoPipeline(source_lang, target_lang) as a C-linkage function with default symbol visibility that returns a signed 32-bit integer status code (0 for success, negative value for failure)
3. THE Dart_FFI_Bridge SHALL expose StopEchoPipeline() as a C-linkage function with default symbol visibility that returns a signed 32-bit integer status code (0 for success, negative value for failure)
4. THE Dart_FFI_Bridge SHALL expose RegisterEchoMessagePort(dart_port_id) as a C-linkage function with default symbol visibility that accepts a 64-bit integer Dart Native Port identifier and returns a signed 32-bit integer status code (0 for success, negative value for failure)
5. WHILE a Native Port is registered, THE Dart_FFI_Bridge SHALL push messages to the registered port containing a type field distinguishing ASR-partial, ASR-confirmed, translation-streaming, translation-complete, TTS-started, TTS-complete, and error message types
6. IF StopEchoPipeline() is called when no pipeline is active, THEN THE Dart_FFI_Bridge SHALL return a negative status code indicating no active session without altering engine state
7. IF StartEchoPipeline() or StopEchoPipeline() is called before RegisterEchoMessagePort() has established a valid port, THEN THE Dart_FFI_Bridge SHALL return a negative status code indicating no registered message port
8. IF RegisterEchoMessagePort() is called while a port is already registered, THEN THE Dart_FFI_Bridge SHALL replace the previously registered port with the new port identifier

### Requirement 16: Model Quantization and Format

**User Story:** As a developer, I want all models in GGUF/INT4 format, so that they fit within device memory constraints while maintaining inference quality.

#### Acceptance Criteria

1. THE Engine SHALL load and execute models exclusively in GGUF format with INT4 quantization
2. THE ASR model (FunASR-Nano, 0.6B parameters) SHALL occupy no more than 165MB on disk
3. THE LLM model (Qwen3-4B-Instruct, 4.0B parameters) SHALL occupy no more than 2.4GB on disk
4. THE TTS model (Qwen3-TTS-Streaming, 0.5B parameters) SHALL occupy no more than 275MB on disk
5. IF a model file is not in GGUF/INT4 format, THEN THE Engine SHALL reject the file and report a format error message to the UI_Shell via the Dart_FFI_Bridge indicating which model failed validation
6. THE total combined disk footprint of all three models (ASR, LLM, TTS) SHALL not exceed 2.85GB
