---
kind: logging_system
name: Ad-hoc Platform-Specific Logging (No Centralized System)
category: logging_system
scope:
    - '**'
source_files:
    - native/hal/android/hal_accelerator_android.c
    - native/hal/android/hal_audio_android.c
    - native/hal/android/hal_memory_android.c
    - native/hal/android/hal_thermal_android.c
    - native/hal/android/hal_thread_android.c
    - native/hal/ios/hal_accelerator_ios.m
    - native/hal/ios/hal_audio_ios.m
    - native/hal/ios/hal_thermal_ios.m
    - native/src/tts_stage.cpp
---

This repository does not implement a centralized logging system. Instead, each platform layer uses its own native logging primitives directly:

- **Android HAL** (`native/hal/android/*.c`): Defines per-module `LOG_TAG` macros and wraps `__android_log_print` with `LOGI/LOGW/LOGE` helpers. Tags follow the `QwenEcho_<Subsystem>` convention (e.g. `QwenEcho_Accel`, `QwenEcho_Audio`).
- **iOS HAL** (`native/hal/ios/*.m`): Uses `NSLog` directly with `[HAL <Subsystem>]`-prefixed messages.
- **Core C++ engine** (`native/src/*.cpp`): No structured logger; only a single `std::fprintf(stderr, ...)` in `tts_stage.cpp` and no other log calls found.
- **Dart shell** (`lib/**/*.dart`): No logging framework is imported or used — there are no `print()`, `debugPrint()`, or third-party logger usages in the Dart codebase.

There is no shared logging abstraction, no log-level configuration, no structured-field convention, and no bridge that routes native logs into the Flutter console. Each subsystem independently chooses its platform sink.