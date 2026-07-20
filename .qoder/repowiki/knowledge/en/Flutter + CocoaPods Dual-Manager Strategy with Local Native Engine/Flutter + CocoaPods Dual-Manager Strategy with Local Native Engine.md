---
kind: dependency_management
name: Flutter + CocoaPods Dual-Manager Strategy with Local Native Engine
category: dependency_management
scope:
    - '**'
source_files:
    - pubspec.yaml
    - ios/Podfile
    - ios/Podfile.lock
    - native/qwen_echo_engine.podspec
---

QwenEcho manages dependencies through two coordinated package managers, each owning a distinct layer of the stack:

1. Dart/Flutter — pubspec.yaml
   - Declares runtime deps (ffi, path_provider, file_picker) and dev deps (flutter_test, flutter_lints).
   - SDK constraints pin Dart >=3.2.0 <4.0.0 and Flutter >=3.16.0.
   - The app is marked publish_to: none; no private Dart registry is configured.
   - pubspec.lock is explicitly gitignored, so Dart dependency versions are resolved at build time rather than pinned in VCS.

2. iOS native — CocoaPods (ios/Podfile + native/qwen_echo_engine.podspec)
   - The iOS wrapper targets platform 16.0 and disables CocoaPods analytics (COCOAPODS_DISABLE_STATS=true) to avoid synchronous network calls during builds.
   - The QwenEcho C/C++ engine lives under native/ and is consumed as a local path pod (pod 'qwen_echo_engine', :path => '../native'); it is never published to a remote spec repo.
   - The podspec compiles src/**/*.{cpp,c}, hal/ios/**/*.{m,c}, and public headers into a static framework (static_framework = true, use_frameworks! deliberately omitted) so that FFI symbols land directly in the Runner binary. A -Wl,-force_load,... linker flag prevents dead-stripping of symbols only referenced from Dart via FFI.
   - System frameworks required by the native code (CoreML, Metal, MetalPerformanceShaders, AVFoundation, AudioToolbox) are declared in the podspec.

3. Lockfiles & reproducibility
   - ios/Podfile.lock is committed and records exact checksums for both the Flutter engine and the local qwen_echo_engine pod, giving deterministic iOS builds.
   - pubspec.lock is intentionally not versioned; Dart dependency resolution is left to the developer's environment.

Conventions developers should follow
- Add new Dart packages only in pubspec.yaml under dependencies or dev_dependencies; do not commit pubspec.lock.
- For any new iOS-native code, extend native/qwen_echo_engine.podspec (sources, headers, compiler flags, system frameworks) and reference it from ios/Podfile via the existing local-path pattern — do not publish the pod to a remote registry.
- Keep ios/Podfile.lock in sync after changing the Podfile or podspec; run pod install before committing.
- Do not enable use_frameworks! in the Podfile; the project relies on static linking so Dart FFI can resolve symbols at runtime.