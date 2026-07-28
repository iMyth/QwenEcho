# QwenEcho

On-device, air-gapped simultaneous interpretation app for iOS. Runs three AI models entirely offline — no cloud, no network, no data leaves the device.

Place the phone between two speakers face-to-face: each sees their own speech transcribed while hearing (and reading) the translation from the other speaker.

## What it does

| Stage | Model | Size | Notes |
|-------|-------|------|-------|
| **ASR** | SenseVoice-Small (sherpa-onnx) | ~250 MB | 52 languages, INT8 ONNX |
| **LLM** | Qwen3.5-0.8B-Instruct (llama.cpp GGUF) | ~500 MB | Q4_K_M quantization, bilingual translation |
| **TTS** | iOS system voices (AVSpeechSynthesizer) | 0 MB | Uses built-in enhanced/premium voices |

Translation uses a sliding context window of the last three source/target pairs for coherence. Qwen3 thinking mode is disabled so output goes straight to content.

## Project Structure

```
QwenEcho/
├── lib/                            # Flutter UI Shell (Dart)
│   ├── main.dart                   # App entry point → HomeScreen
│   └── src/
│       ├── echo_engine.dart        # Lifecycle facade (MethodChannel + LLM bridge)
│       ├── messages.dart           # Typed message hierarchy (EventChannel → UI)
│       ├── llm/
│       │   └── llm_service.dart    # llamadart wrapper + prompt template
│       ├── tts/
│       │   └── tts_service.dart    # TTS MethodChannel bridge
│       ├── model/
│       │   ├── model_catalog.dart  # Static list of required models
│       │   └── model_repository.dart # Sandbox provisioning + validation
│       └── ui/
│           ├── home_screen.dart        # Setup + language picker + InterpretationScreen
│           ├── split_view.dart         # Bilateral split (top rotated 180°)
│           ├── speaker_half.dart       # One half of the split (partial → confirmed)
│           ├── model_config_screen.dart # Model import/delete management
│           ├── status_bar.dart         # OFFLINE badge + thermal indicator
│           ├── warning_overlay.dart    # Transient memory/latency alerts
│           └── languages.dart          # 10 supported languages + flags
├── ios/Runner/SwiftEngine/         # iOS Native Engine (Swift)
│   ├── AudioCapture.swift          # AVAudioEngine mic tap (48kHz Float32 → 16kHz Int16)
│   ├── VoiceActivityDetector.swift # Energy-based VAD + segment locking
│   ├── AsrStage.swift              # sherpa-onnx inference wrapper
│   ├── SherpaOnnx.swift            # Swift API over the CSherpaOnnx C module
│   ├── PipelineController.swift    # Audio → VAD → ASR orchestration
│   ├── ThermalMonitor.swift        # ProcessInfo thermal state polling
│   ├── TtsPlayer.swift             # AVSpeechSynthesizer wrapper
│   ├── EnginePlugin.swift          # MethodChannel/EventChannel bridge
│   ├── MessageStream.swift         # Event sink dispatcher
│   └── EchoMessage.swift           # Native message type definitions
├── test/                           # Flutter widget tests
├── pubspec.yaml
└── README.md
```

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  Flutter UI Shell (Dart)                                    │
│  ┌──────────┐  ┌───────────┐  ┌──────────┐  ┌───────────┐ │
│  │ Home +   │  │ SplitView │  │ TtsServ  │  │ StatusBar │ │
│  │ Setup    │  │ (bilateral│  │ (Method  │  │ + Warning │ │
│  │          │  │  rotated) │  │  Channel)│  │  Overlay  │ │
│  └──────────┘  └───────────┘  └──────────┘  └───────────┘ │
└────────────────────────┬────────────────────────────────────┘
                         │ MethodChannel (commands)
                         │ EventChannel  (events)
                         │ llamadart FFI   (LLM directly)
┌────────────────────────┴────────────────────────────────────┐
│  iOS Native Engine (Swift)                                  │
│                                                             │
│  AVAudioEngine tap (48kHz Float32)                          │
│        ↓ Float32 → Int16 + 48→16kHz linear downsample       │
│  VoiceActivityDetector (energy threshold + state machine)   │
│        ↓ locked segment                                     │
│  AsrStage (sherpa-onnx SenseVoice-Small)                    │
│        ↓ confirmed text via EventChannel                    │
│  Dart LlmService → Qwen3.5-0.8B GGUF → tokens             │
│        ↓ translation stream via TranslationStreamMessage    │
│  TtsService (AVSpeechSynthesizer) speaks the translation    │
│                                                             │
│  ThermalMonitor (Normal / Throttle / Critical)              │
└─────────────────────────────────────────────────────────────┘
```

**Key design decisions:**

- ASR runs in Swift (native); LLM runs in Dart (llamadart). Both halves of the pipeline are connected via the Flutter event stream.
- Audio format conversion happens in a single pass in `processBuffer`: Float32 → Int16 + 48 kHz → 16 kHz linear interpolation. AVAudioEngine's tap must be installed with the input node's native format — no conversion allowed at tap time.
- AVAudioEngine is reset before every `start()` and in `stop()` to prevent "invalid reuse after initialization failure" after a failed start.
- `flush-on-stop`: VAD's pending accumulator is pushed through ASR before teardown, so pressing Stop doesn't discard what the user just said.
- Audio session uses `.playAndRecord` + `.defaultToSpeaker` so mic input and TTS output coexist.
- The engine is created on the home screen and lives for the app's lifetime. Entering/leaving the interpretation screen does not reload models.

## Requirements

- **iOS**: 16+, arm64, 4GB+ RAM recommended
- **Models**: ~750 MB total disk space for ASR package + LLM GGUF
- **Build**: Xcode 15+, Flutter 3.16+, Swift 5.9+
- **Simulator**: Enable Mac mic passthrough via Simulator menu → Features → Audio Input → `<your Mac's mic>`

Android is **not yet implemented** — only the iOS Swift engine exists.

## Building

### Prerequisites

Models must be provisioned locally into the app sandbox. QwenEcho does NOT download anything from the network. Two ways to get models in:

1. **Bundle in Flutter assets** — drop model files under `assets/models/` in the project; `ModelRepository` resolves them automatically at runtime.
2. **Import via Files app** — place model files in the app's Files container; `ModelConfigScreen` validates and adopts them.

Required assets:
- **ASR**: `SenseVoiceSmall-onnx/` directory containing `model.int8.onnx` + `tokens.txt`
- **LLM**: `Qwen3.5-0.8B-Q4_K_M.gguf` file (must pass GGUF magic-byte validation)

#### iOS native dependencies

ASR uses the [`genericgroup/sherpa-onnx-spm`](https://github.com/genericgroup/sherpa-onnx-spm)
remote Swift package (Xcode resolves it automatically on first build). The
package ships a combined XCFramework — sherpa-onnx + ONNX Runtime statically
merged — so no separate `prepare.sh` step is needed. The Swift API wrapper
(`ios/Runner/SwiftEngine/SherpaOnnx.swift`) is vendored directly in the
Runner target.

### Build & Run

```bash
flutter pub get
flutter run                # picks a connected device / simulator
flutter build ios --release
```

### First Launch

1. Open the app → **Home screen** appears.
2. If models are missing, the status card turns amber and the "Start" button is disabled. Tap **Settings** → follow the import flow.
3. Once both models show green, pick source and target language (e.g. 🇨🇳 Chinese → 🇺🇸 English).
4. Tap **Start Interpreting** → app asks for microphone permission → split view appears.
5. The pipeline starts automatically. Speak into the mic; your speech transcribes in your half (bottom, normal orientation) and the translation appears + is spoken aloud in the opposing half (top, rotated 180°).
6. Use the central mic button to pause/resume. Use the speaker icon to mute TTS output.

## UI Walkthrough

| Screen | Purpose |
|--------|---------|
| **Home screen** | Model status summary, language picker, start button |
| **Model Config** | List required models, delete + re-import |
| **Interpretation screen** | Bilateral split view + start/stop/mute control bar + status overlay |

The split view locks to portrait orientation and uses immersive-sticky mode (no system bars). The top half is rotated 180° so the person across the table can read the translation right-side-up.

## Supported Languages

| Code | Language | Flag |
|------|----------|------|
| zh | Chinese | 🇨🇳 |
| en | English | 🇺🇸 |
| ja | Japanese | 🇯🇵 |
| ko | Korean | 🇰🇷 |
| fr | French | 🇫🇷 |
| es | Spanish | 🇪🇸 |
| de | German | 🇩🇪 |
| ru | Russian | 🇷🇺 |
| ar | Arabic | 🇸🇦 |
| pt | Portuguese | 🇵🇹 |

ASR coverage (SenseVoice) extends to 52 languages; we surface only the ten with reliable iOS TTS voices. Adding more is a matter of appending to `kSupportedLanguages` in `lib/src/ui/languages.dart`.

## Message Flow

```
ASR confirms a segment
  └─→ AsrConfirmedMessage (EventChannel → Dart)
        ├─→ UI: origin speaker's half, gray → white
        └─→ EchoEngine._runTranslation()
              └─→ LlmService.translate() streams tokens
                    └─→ TranslationStreamMessage per token
                          └─→ UI: opposing speaker's half, green text
                    └─→ TranslationDoneMessage when finished
                          └─→ TtsService.speak(text, tgtLang)
                                └─→ AVSpeechSynthesizer plays aloud
```

## Thermal Management

| Temperature | Mode | Behavior |
|-------------|------|----------|
| ≤42°C | Normal | Full performance |
| >43°C | Throttle | Reduced LLM context |
| >50°C | Critical | Pipeline paused, resumes at ≤45°C |

The thermal state is surfaced via `ThermalStateMessage` and rendered by `StatusBar`.

## Offline Policy

QwenEcho makes **zero network requests** after model provisioning:
- No telemetry, analytics, crash-reporting, or update checks
- Model files stored within application sandbox only
- `ModelRepository` performs NO network I/O — it only validates files already on disk

## Testing

```bash
flutter test                     # widget tests
xcodebuild test -project ios/Runner.xcodeproj -scheme Runner  # native tests
```

For simulator testing without a microphone, use `engine.testInject('你好世界')` from a debug console — it posts a fake ASR segment through the pipeline without needing audio input.

## Roadmap

- **Android engine** — mirror the Swift pipeline in Kotlin/JNI with NNAPI + AAudio
- **Qwen3-TTS-Streaming** — replace system voices with the ~250MB streaming TTS model for higher-quality output
- **C++ native engine** — unify ASR/LLM/TTS under a single cross-platform C++ core with a lock-free SPSC ring buffer (the architecture originally described)
- **Bluetooth audio routing** — per-device output so each speaker hears through their own earbud
- **Conversation memory** — longer sliding context and optional session summary

## License

Proprietary. All rights reserved.
