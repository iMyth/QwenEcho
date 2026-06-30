# QwenEcho

On-device, air-gapped simultaneous interpretation app. Runs three AI models entirely offline on mobile hardware — no cloud, no network, no data leaves the device.

## What it does

QwenEcho provides real-time bilateral translation between two speakers in a face-to-face setting. Place the phone between two people, and each sees their own speech transcribed while hearing/reading the translation from the other speaker.

- **ASR**: FunASR-Nano (~150MB) — speech to text in 52 languages
- **LLM**: Qwen3-4B-Instruct (~2.2GB) — bilingual translation with context
- **TTS**: Qwen3-TTS-Streaming (~250MB) — text to speech synthesis

All models use GGUF/INT4 quantization for efficient on-device inference.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  Flutter UI Shell (Dart)                                │
│  ┌──────────┐  ┌────────────┐  ┌───────────────────┐   │
│  │Split View│  │Port Manager│  │  Status Bar       │   │
│  └──────────┘  └────────────┘  └───────────────────┘   │
└────────────────────────┬────────────────────────────────┘
                         │ Dart FFI (4 functions + Native Port)
┌────────────────────────┴────────────────────────────────┐
│  C/C++ Native Engine                                    │
│                                                         │
│  Audio Collector → Ring Buffer → VAD/Segmenter          │
│       → ASR Stage → LLM Stage → TTS Stage → Speaker    │
│                                                         │
│  Thermal Monitor │ Memory Monitor │ Latency Tracker     │
└─────────────────────────────────────────────────────────┘
```

**Key design decisions:**
- Lock-free SPSC ring buffer and bounded queues for zero-contention pipeline
- Cascade truncation: downstream stages begin before upstream finishes
- Three-mode thermal state machine (Normal/Throttle/Critical) prevents overheating
- Platform abstraction layer (HAL) isolates Android/iOS specifics

## Project Structure

```
QwenEcho/
├── lib/                        # Flutter UI Shell (Dart)
│   ├── main.dart               # App entry point + message routing
│   ├── qwen_echo.dart          # Barrel exports
│   └── src/
│       ├── echo_engine.dart    # High-level engine facade
│       ├── messages.dart       # Typed Native Port message classes
│       ├── native_bridge.dart  # Dart FFI bindings
│       ├── port_manager.dart   # Native Port receive/dispatch
│       └── ui/
│           ├── line_buffer.dart    # 50-line buffer with 3-color states
│           ├── speaker_half.dart   # One half of the bilateral view
│           ├── split_view.dart     # 50/50 split, top rotated 180°
│           ├── status_bar.dart     # OFFLINE badge + thermal indicator
│           ├── text_display.dart   # Auto-scrolling text renderer
│           └── warning_overlay.dart # Transient memory/latency alerts
├── native/                     # C/C++ Engine
│   ├── CMakeLists.txt          # Build system (C++17, arm64, RapidCheck)
│   ├── include/                # Public headers
│   │   ├── echo_types.h       # Shared enums and structs
│   │   ├── audio_ring_buffer.h
│   │   ├── bounded_spsc_queue.h
│   │   ├── engine_manager.h
│   │   ├── pipeline_controller.h
│   │   ├── model_loader.h
│   │   ├── ffi_bridge.h
│   │   ├── native_port.h
│   │   ├── audio_collector.h
│   │   ├── sentence_segmenter.h
│   │   ├── asr_stage.h
│   │   ├── llm_stage.h
│   │   ├── tts_stage.h
│   │   ├── thermal_monitor.h
│   │   ├── memory_monitor.h
│   │   ├── latency_tracker.h
│   │   └── offline_policy.h
│   ├── src/                    # Implementation files
│   ├── hal/                    # Platform Abstraction Layer
│   │   ├── hal_accelerator.h   # NPU/GPU inference
│   │   ├── hal_audio.h        # Audio capture/output
│   │   ├── hal_thermal.h      # Temperature polling
│   │   ├── hal_memory.h       # RSS monitoring
│   │   ├── hal_thread.h       # RT priority
│   │   ├── android/           # NNAPI, AAudio, AThermal
│   │   └── ios/               # CoreML, AVAudioEngine, ProcessInfo
│   └── tests/                  # RapidCheck property-based tests
├── test/                       # Flutter widget tests
├── pubspec.yaml
└── README.md
```

## Requirements

- **Android**: 11+ (API 30), arm64-v8a, 4GB+ RAM recommended
- **iOS**: 16+, arm64, 4GB+ RAM recommended
- **Models**: ~2.6GB total disk space for GGUF/INT4 model files
- **Build**: CMake 3.20+, NDK r21+ (Android), Xcode 14+ (iOS), Flutter 3.16+

## Building

### Native Engine

```bash
cd native
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

For Android cross-compilation:
```bash
cmake .. -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
         -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30
```

### Flutter App

```bash
flutter pub get
flutter build apk --release    # Android
flutter build ios --release    # iOS
```

### Running Tests

Native (property-based tests with RapidCheck):
```bash
cd native/build
ctest --output-on-failure
```

Flutter:
```bash
flutter test
```

## Performance Targets

| Stage | Budget (Normal) | Budget (Throttle) |
|-------|----------------|-------------------|
| ASR first-character | ≤200ms | ≤200ms |
| LLM first-token | ≤450ms | ≤450ms |
| TTS time-to-first-audio | ≤100ms | ≤100ms |
| **E2E total** | **≤800ms** | **≤1200ms** |

## Thermal Management

| Temperature | Mode | Behavior |
|-------------|------|----------|
| ≤42°C | Normal | Full performance, 512-token LLM context |
| >43°C | Throttle | Reduced context (256 tokens), 8kHz ASR |
| >50°C | Critical | Pipeline paused, resumes at ≤45°C |

## Memory Budget

| Platform | Limit | Level 1 (85%) | Level 2 (95%) |
|----------|-------|---------------|---------------|
| Android | 2.5GB | Release KV caches | Stop pipeline |
| iOS | 2.0GB | Release KV caches | Stop pipeline |

## FFI Interface

Four C-linkage entry points exposed to Dart:

```c
int32_t InitQwenEchoEngine(const char* asr_path, const char* llm_path, const char* tts_path);
int32_t StartEchoPipeline(const char* source_lang, const char* target_lang);
int32_t StopEchoPipeline(void);
int32_t RegisterEchoMessagePort(int64_t dart_port_id);
```

All return 0 on success, negative `EchoErrorCode` on failure.

## Offline Policy

QwenEcho makes **zero network requests** after model provisioning:
- No telemetry, analytics, crash-reporting, or update checks
- Model files stored within application sandbox only
- Compile-time symbol poisoning prevents accidental network API usage
- Runtime verification confirms no network libraries are loaded
- Launches and operates without network interfaces present

## License

Proprietary. All rights reserved.
