/// High-level QwenEcho engine API.
///
/// Combines [NativeBridge] and [PortManager] into a single facade that the
/// UI layer interacts with. Manages the init → start → stop lifecycle.
library;

import 'dart:async';

import 'messages.dart';
import 'native_bridge.dart';
import 'port_manager.dart';

/// Lifecycle state of the [EchoEngine] from the Dart side.
enum EchoEngineState {
  /// Engine has not been initialized.
  uninitialized,

  /// Engine is initialized and ready to start a pipeline session.
  ready,

  /// Pipeline is actively running (interpreting).
  running,
}

/// Facade over the native QwenEcho engine.
///
/// Usage:
/// ```dart
/// final engine = EchoEngine();
/// engine.messages.listen((msg) { /* handle */ });
/// engine.init(asrPath: '...', llmPath: '...', ttsPath: '...');
/// engine.start(srcLang: 'zh', tgtLang: 'en');
/// // ... interpretation running ...
/// engine.stop();
/// engine.dispose();
/// ```
class EchoEngine {
  final NativeBridge _bridge;
  late final PortManager _portManager;

  EchoEngineState _state = EchoEngineState.uninitialized;

  /// Current lifecycle state.
  EchoEngineState get state => _state;

  /// Stream of typed messages from the native engine (ASR, translation,
  /// TTS events, errors, diagnostics).
  Stream<EchoMessage> get messages => _portManager.messages;

  /// Create an [EchoEngine] using the default platform native library.
  EchoEngine() : _bridge = NativeBridge() {
    _portManager = PortManager(_bridge);
  }

  /// Create an [EchoEngine] with a custom [NativeBridge] (e.g. for testing).
  EchoEngine.withBridge(this._bridge) {
    _portManager = PortManager(_bridge);
  }

  /// Initialize the engine with model file paths.
  ///
  /// Registers the Native Port, then calls [InitQwenEchoEngine].
  /// After success, the engine is in [EchoEngineState.ready].
  ///
  /// Throws [EchoEngineException] on failure.
  void init({
    required String asrPath,
    required String llmPath,
    required String ttsPath,
  }) {
    print('[EchoEngine] Initializing with models:');
    print('[EchoEngine]   ASR: $asrPath');
    print('[EchoEngine]   LLM: $llmPath');
    print('[EchoEngine]   TTS: $ttsPath');

    // Initialize the Dart API DL subsystem so native code can post messages.
    _bridge.initDartApiDL();
    print('[EchoEngine] Dart API DL initialized');

    // Register port so the engine can send init status messages.
    _portManager.register();
    print('[EchoEngine] Native port registered');

    _bridge.initEngine(asrPath, llmPath, ttsPath);
    print('[EchoEngine] Native engine initialized (models loaded)');

    _state = EchoEngineState.ready;
  }

  /// Start the interpretation pipeline with the given language pair.
  ///
  /// [srcLang] and [tgtLang] are ISO 639-1 codes (e.g. "zh", "en").
  /// The engine must be in [EchoEngineState.ready].
  ///
  /// Throws [EchoEngineException] on failure (e.g. unsupported language,
  /// engine not ready, session already active).
  void start({required String srcLang, required String tgtLang}) {
    print('[EchoEngine] Starting pipeline: $srcLang \u2192 $tgtLang');
    _bridge.startPipeline(srcLang, tgtLang);
    print('[EchoEngine] Pipeline started');
    _state = EchoEngineState.running;
  }

  /// Stop the active interpretation pipeline.
  ///
  /// Locked segments are processed; unlocked audio is discarded.
  /// Returns to [EchoEngineState.ready] on success.
  ///
  /// Throws [EchoEngineException] on failure.
  void stop() {
    _bridge.stopPipeline();
    _state = EchoEngineState.ready;
  }

  /// Dispose of all Dart-side resources.
  ///
  /// Does NOT stop the native engine — call [stop] first if a session is
  /// active.
  void dispose() {
    _portManager.dispose();
  }
}
