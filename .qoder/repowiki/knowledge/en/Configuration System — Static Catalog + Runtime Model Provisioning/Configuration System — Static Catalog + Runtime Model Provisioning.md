---
kind: configuration_system
name: Configuration System — Static Catalog + Runtime Model Provisioning
category: configuration_system
scope:
    - '**'
source_files:
    - lib/src/model/model_catalog.dart
    - lib/src/model/model_repository.dart
    - lib/src/ui/model_config_screen.dart
    - lib/src/native_bridge.dart
    - native/include/echo_types.h
---

QwenEcho does not use a traditional configuration file or environment-variable system. Instead, runtime configuration is split into two layers:

1. Static model catalog (compile-time / source-level) — lib/src/model/model_catalog.dart defines the three required GGUF models (kRequiredModels) as an immutable list of ModelSpec entries containing kind, display name, canonical filename, and maximum on-disk size ceiling. This is the single source of truth for which models are expected and where they must live inside the app sandbox. There is no external YAML/TOML/env to parse; changing a model means editing this Dart constant.

2. Runtime model provisioning (user-driven, air-gapped) — lib/src/model/model_repository.dart resolves the application support directory via path_provider, creates a private models/ subdirectory, validates imported files against the catalog's filename and size constraints, checks the GGUF magic header, and exposes import/delete/status APIs. The UI in lib/src/ui/model_config_screen.dart drives user selection through file_picker and streams copy progress back to the view. No persistent settings store (SharedPreferences, JSON, plist) is used — the only persisted state is the model files themselves.

3. Native engine configuration — The C/C++ engine accepts its configuration exclusively through the EngineConfig struct in native/include/echo_types.h (model paths, language pair, ring-buffer capacity, thermal/memory thresholds, LLM context sizes, segmenter timing, sample rates). These values are passed at initialization time from Dart via NativeBridge.initEngine(asrPath, llmPath, ttsPath) plus startPipeline(srcLang, tgtLang); there is no config file read by the native layer.

4. Platform build configuration — iOS uses CocoaPods (Podfile, qwen_echo_engine.podspec) and Xcode project settings to statically link the native library into the Runner process; Android/Linux load libqwen_echo.so dynamically via DynamicLibrary.open. The Dart FFI loader in native_bridge.dart selects the correct library per platform.

Conventions developers should follow:
- Add or change a required model by editing kRequiredModels in model_catalog.dart; do not introduce separate config files.
- All model artifacts must be placed by the user through the Model Configuration screen (or programmatically via ModelRepository.importModel); never hard-code absolute paths outside the sandbox.
- Native engine tuning parameters (thermal thresholds, memory limits, ring buffer size, LLM context) are supplied at init time via the EngineConfig struct fields — keep them close to their usage sites rather than scattering them across env vars.
- Do not add network-based downloads or remote config endpoints; the app is designed to be fully air-gapped.