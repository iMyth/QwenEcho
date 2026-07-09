---
kind: build_system
name: Flutter + CMake/CocoaPods Hybrid Build System
category: build_system
scope:
    - '**'
source_files:
    - native/CMakeLists.txt
    - native/qwen_echo_engine.podspec
    - ios/Podfile
    - pubspec.yaml
---

QwenEcho uses a hybrid build system that composes three layers: the Flutter app shell, a C/C++ native engine built with CMake, and an iOS CocoaPods wrapper that embeds the native library as a static framework consumed via Dart FFI.

**Native engine (C/C++)**
- `native/CMakeLists.txt` defines a CMake 3.20 project (`qwen_echo_engine`) compiled to C++17 / C11 with `-Wall -Wextra -Wpedantic`. Sources are collected via `file(GLOB_RECURSE)` from `src/` and `hal/<platform>/`, and the resulting static library is exposed through `include/ffi_bridge.h` and `include/echo_types.h`.
- Platform selection is conditional: Android HAL sources under `hal/android/*.c` are added only when `ANDROID` is defined; iOS/macOS HAL sources under `hal/ios/*.m` are included by the podspec. Android-specific linker flags enforce 16 KB page alignment for API 35+.
- Native unit tests are opt-in via the `QWEN_ECHO_BUILD_TESTS` flag and use RapidCheck fetched through CMake `FetchContent`; each test file becomes its own executable registered with `add_test`.

**iOS integration (CocoaPods)**
- `native/qwen_echo_engine.podspec` packages the same source tree as a static framework targeting iOS 16+. It declares public headers (`ffi_bridge.h`, `echo_types.h`) and links CoreML, Metal, MetalPerformanceShaders, AVFoundation, and AudioToolbox.
- A critical `user_target_xcconfig` injects `-Wl,-force_load,...libqwen_echo_engine.a` and disables `DEAD_CODE_STRIPPING` so symbols reachable only at runtime via Dart FFI survive the link.
- `ios/Podfile` installs Flutter pods plus the local pod (`pod 'qwen_echo_engine', :path => '../native'`) and explicitly avoids `use_frameworks!` because the engine must be a static `.a` linked into the Runner binary.

**Flutter layer**
- `pubspec.yaml` pins Flutter ≥3.16.0 and Dart ≥3.2.0, depends on `ffi ^2.1.3` and `path_provider` for sandbox model access, and lists `flutter_lints` as the only dev dependency. No network dependencies are declared — the app is air-gapped.
- The README documents the standard Flutter CLI commands (`flutter build apk --release`, `flutter build ios --release`, `flutter test`) as the top-level entry points.

**Build flow & conventions**
- Developers invoke `cmake .. && cmake --build .` in `native/` to build the native engine standalone, or rely on `flutter build ios` which triggers CocoaPods → Xcode → force-load of the static library.
- Cross-compilation for Android requires passing the NDK toolchain file (`-DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake`).
- There is no shared Makefile, Dockerfile, CI pipeline, or release automation script in this branch — builds are driven entirely by CMake (native) and Flutter's built-in tooling (Dart/iOS/Android). Version numbers live in both `native/CMakeLists.txt` and `pubspec.yaml` and must be kept in sync manually.