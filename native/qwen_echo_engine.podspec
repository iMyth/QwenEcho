Pod::Spec.new do |s|
  s.name             = 'qwen_echo_engine'
  s.version          = '0.1.0'
  s.summary          = 'QwenEcho native C/C++ engine for on-device interpretation.'
  s.homepage         = 'https://github.com/placeholder/qwen_echo'
  s.license          = { :type => 'Proprietary' }
  s.author           = { 'QwenEcho' => 'dev@example.com' }
  s.source           = { :path => '.' }

  s.ios.deployment_target = '16.0'
  s.static_framework = true

  # ─── Source files ──────────────────────────────────────────────────────────
  s.source_files = [
    'src/**/*.{cpp,c}',
    'hal/ios/**/*.{m,c}',
    'include/**/*.h',
    'hal/*.h',
  ]

  s.public_header_files = [
    'include/ffi_bridge.h',
    'include/echo_types.h',
  ]

  # ─── Compiler settings ────────────────────────────────────────────────────
  s.pod_target_xcconfig = {
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    'CLANG_CXX_LIBRARY' => 'libc++',
    'GCC_C_LANGUAGE_STANDARD' => 'c11',
    'OTHER_CFLAGS' => '-Wall -Wextra -Wno-unused-parameter',
    'OTHER_CPLUSPLUSFLAGS' => '-Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations',
    'HEADER_SEARCH_PATHS' => '"${PODS_TARGET_SRCROOT}/include" "${PODS_TARGET_SRCROOT}/hal" "${PODS_TARGET_SRCROOT}/third_party/llama.cpp/include" "${PODS_TARGET_SRCROOT}/third_party/llama.cpp/ggml/include"',
  }

  # ─── llama.cpp pre-built static libraries ─────────────────────────────────
  # Built from native/third_party/llama.cpp via CMake for iOS simulator.
  # Rebuild for device targets when deploying to physical devices.
  s.vendored_libraries = [
    'third_party/lib/ios-sim/libllama.a',
    'third_party/lib/ios-sim/libggml.a',
    'third_party/lib/ios-sim/libggml-base.a',
    'third_party/lib/ios-sim/libggml-cpu.a',
  ]

  # Force-load the engine static library so FFI symbols (called at runtime
  # via Dart FFI dlsym, with no compile-time reference) are linked into the
  # Runner binary. Disable dead code stripping as additional safety.
  s.user_target_xcconfig = {
    'OTHER_LDFLAGS' => '$(inherited) -Wl,-force_load,${PODS_CONFIGURATION_BUILD_DIR}/qwen_echo_engine/libqwen_echo_engine.a',
    'DEAD_CODE_STRIPPING' => 'NO',
  }

  # ─── Frameworks ───────────────────────────────────────────────────────────
  s.frameworks = ['CoreML', 'Metal', 'MetalPerformanceShaders', 'AVFoundation', 'AudioToolbox', 'Accelerate']

  # ─── Platform guard ───────────────────────────────────────────────────────
  s.platform = :ios, '16.0'
end
