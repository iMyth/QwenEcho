/// Dart FFI bindings for the QwenEcho native engine.
///
/// Loads the native library and exposes the 4 C-linkage entry points as
/// type-safe Dart methods. Throws [EchoEngineException] on non-zero returns.
library;

import 'dart:ffi';
import 'dart:io' show Platform;

import 'package:ffi/ffi.dart';

// ---------------------------------------------------------------------------
// FFI type definitions
// ---------------------------------------------------------------------------

/// C signature: int32_t InitQwenEchoEngine(const char*, const char*, const char*)
typedef _InitEngineNative = Int32 Function(
    Pointer<Utf8> asrPath, Pointer<Utf8> llmPath, Pointer<Utf8> ttsPath);
typedef _InitEngineDart = int Function(
    Pointer<Utf8> asrPath, Pointer<Utf8> llmPath, Pointer<Utf8> ttsPath);

/// C signature: int32_t StartEchoPipeline(const char*, const char*)
typedef _StartPipelineNative = Int32 Function(
    Pointer<Utf8> sourceLang, Pointer<Utf8> targetLang);
typedef _StartPipelineDart = int Function(
    Pointer<Utf8> sourceLang, Pointer<Utf8> targetLang);

/// C signature: int32_t StopEchoPipeline(void)
typedef _StopPipelineNative = Int32 Function();
typedef _StopPipelineDart = int Function();

/// C signature: int32_t RegisterEchoMessagePort(int64_t)
typedef _RegisterPortNative = Int32 Function(Int64 dartPortId);
typedef _RegisterPortDart = int Function(int dartPortId);

// ---------------------------------------------------------------------------
// Error codes (mirrors EchoErrorCode in echo_types.h)
// ---------------------------------------------------------------------------

/// Error codes returned by the native engine.
///
/// Values mirror the C enum `EchoErrorCode`.
abstract final class EchoErrorCode {
  static const int ok = 0;
  static const int notInitialized = -1;
  static const int alreadyInit = -2;
  static const int modelMissing = -3;
  static const int modelInvalid = -4;
  static const int modelPermission = -5;
  static const int memory = -6;
  static const int unsupportedLang = -7;
  static const int sessionActive = -8;
  static const int noSession = -9;
  static const int noPort = -10;
  static const int engineNotReady = -11;
  static const int thermalCritical = -12;

  /// Human-readable description for an error code.
  static String describe(int code) => switch (code) {
        ok => 'Success',
        notInitialized => 'Engine not initialized',
        alreadyInit => 'Engine already initialized',
        modelMissing => 'Model file missing',
        modelInvalid => 'Model file invalid (bad GGUF header or quantization)',
        modelPermission => 'Model file permission denied',
        memory => 'Memory allocation failed',
        unsupportedLang => 'Unsupported language pair',
        sessionActive => 'Pipeline session already active',
        noSession => 'No active pipeline session',
        noPort => 'No Native Port registered',
        engineNotReady => 'Engine not in Ready state',
        thermalCritical => 'Critical thermal state',
        _ => 'Unknown error ($code)',
      };
}

// ---------------------------------------------------------------------------
// Exception type
// ---------------------------------------------------------------------------

/// Exception thrown when a native engine call returns a non-zero error code.
class EchoEngineException implements Exception {
  /// The raw error code from the native engine.
  final int code;

  /// Human-readable message describing the error.
  final String message;

  const EchoEngineException(this.code, this.message);

  @override
  String toString() => 'EchoEngineException($code): $message';
}

// ---------------------------------------------------------------------------
// NativeBridge
// ---------------------------------------------------------------------------

/// Dart FFI bridge to the QwenEcho native engine.
///
/// Loads the platform-specific shared library and provides typed Dart methods
/// wrapping the 4 C-linkage FFI functions.
class NativeBridge {
  late final DynamicLibrary _lib;
  late final _InitEngineDart _initEngine;
  late final _StartPipelineDart _startPipeline;
  late final _StopPipelineDart _stopPipeline;
  late final _RegisterPortDart _registerPort;

  /// Create a [NativeBridge] loading the native library for the current
  /// platform.
  ///
  /// On Android, loads `libqwen_echo.so`.
  /// On iOS/macOS, loads `libqwen_echo.dylib` (or uses the process itself
  /// when statically linked via a framework).
  NativeBridge() {
    _lib = _loadLibrary();
    _lookupFunctions();
  }

  /// Create a [NativeBridge] using a pre-loaded [DynamicLibrary].
  ///
  /// Useful for testing or when the library is embedded in a framework.
  NativeBridge.fromLibrary(DynamicLibrary library) : _lib = library {
    _lookupFunctions();
  }

  // -------------------------------------------------------------------------
  // Public API
  // -------------------------------------------------------------------------

  /// Initialize the engine with model file paths.
  ///
  /// Loads ASR, LLM, and TTS models from the specified paths.
  /// Must be called before [startPipeline].
  ///
  /// Throws [EchoEngineException] on failure.
  void initEngine(String asrPath, String llmPath, String ttsPath) {
    final pAsr = asrPath.toNativeUtf8();
    final pLlm = llmPath.toNativeUtf8();
    final pTts = ttsPath.toNativeUtf8();
    try {
      final result = _initEngine(pAsr, pLlm, pTts);
      _throwOnError(result);
    } finally {
      calloc.free(pAsr);
      calloc.free(pLlm);
      calloc.free(pTts);
    }
  }

  /// Start the interpretation pipeline with the specified language pair.
  ///
  /// [srcLang] and [tgtLang] are ISO 639-1 language codes (e.g. "zh", "en").
  ///
  /// Throws [EchoEngineException] on failure.
  void startPipeline(String srcLang, String tgtLang) {
    final pSrc = srcLang.toNativeUtf8();
    final pTgt = tgtLang.toNativeUtf8();
    try {
      final result = _startPipeline(pSrc, pTgt);
      _throwOnError(result);
    } finally {
      calloc.free(pSrc);
      calloc.free(pTgt);
    }
  }

  /// Stop the active interpretation pipeline.
  ///
  /// Throws [EchoEngineException] on failure.
  void stopPipeline() {
    final result = _stopPipeline();
    _throwOnError(result);
  }

  /// Register a Dart Native Port for async message delivery.
  ///
  /// [portId] is the [SendPort.nativePort] value.
  ///
  /// Throws [EchoEngineException] on failure.
  void registerPort(int portId) {
    final result = _registerPort(portId);
    _throwOnError(result);
  }

  // -------------------------------------------------------------------------
  // Private helpers
  // -------------------------------------------------------------------------

  static DynamicLibrary _loadLibrary() {
    if (Platform.isAndroid || Platform.isLinux) {
      return DynamicLibrary.open('libqwen_echo.so');
    }
    if (Platform.isIOS || Platform.isMacOS) {
      // On iOS the native code is typically statically linked into the
      // Flutter runner; try the process first, fall back to dylib.
      try {
        return DynamicLibrary.process();
      } catch (_) {
        return DynamicLibrary.open('libqwen_echo.dylib');
      }
    }
    throw UnsupportedError(
      'QwenEcho native library not supported on ${Platform.operatingSystem}',
    );
  }

  void _lookupFunctions() {
    _initEngine = _lib
        .lookupFunction<_InitEngineNative, _InitEngineDart>(
            'InitQwenEchoEngine');
    _startPipeline = _lib
        .lookupFunction<_StartPipelineNative, _StartPipelineDart>(
            'StartEchoPipeline');
    _stopPipeline = _lib
        .lookupFunction<_StopPipelineNative, _StopPipelineDart>(
            'StopEchoPipeline');
    _registerPort = _lib
        .lookupFunction<_RegisterPortNative, _RegisterPortDart>(
            'RegisterEchoMessagePort');
  }

  void _throwOnError(int code) {
    if (code != EchoErrorCode.ok) {
      throw EchoEngineException(code, EchoErrorCode.describe(code));
    }
  }
}
