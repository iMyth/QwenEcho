/// High-level QwenEcho engine API.
///
/// Uses MethodChannel + EventChannel to communicate with the Swift native engine.
/// Manages the init -> start -> stop lifecycle.
library;

import 'dart:async';

import 'package:flutter/services.dart';

import 'messages.dart';

/// Lifecycle state of the [EchoEngine].
enum EchoEngineState {
  /// Engine has not been initialized.
  uninitialized,

  /// Engine is initialized and ready to start a pipeline session.
  ready,

  /// Pipeline is actively running (interpreting).
  running,
}

/// Facade over the native QwenEcho engine (Swift via MethodChannel).
///
/// Usage:
/// ```dart
/// final engine = EchoEngine();
/// engine.messages.listen((msg) { /* handle */ });
/// engine.init(llmPath: '...');
/// engine.start(srcLang: 'zh', tgtLang: 'en');
/// // ... interpretation running ...
/// engine.stop();
/// engine.dispose();
/// ```
class EchoEngine {
  static const _methodChannel = MethodChannel('qwen_echo_engine');
  static const _eventChannel = EventChannel('qwen_echo_events');

  EchoEngineState _state = EchoEngineState.uninitialized;

  /// Current lifecycle state.
  EchoEngineState get state => _state;

  final StreamController<EchoMessage> _messageController =
      StreamController<EchoMessage>.broadcast();

  /// Stream of typed messages from the native engine.
  Stream<EchoMessage> get messages => _messageController.stream;

  StreamSubscription<dynamic>? _eventSubscription;

  /// Create an [EchoEngine].
  EchoEngine() {
    _eventSubscription = _eventChannel.receiveBroadcastStream().listen(
      _onEvent,
      onError: (error) {
        print('[EchoEngine] EventChannel error: $error');
      },
    );
  }

  /// Initialize the engine with the LLM model directory path.
  ///
  /// ASR uses SFSpeechRecognizer (built-in, no model needed).
  /// TTS is deferred to Phase 5.
  ///
  /// Calls the Swift `initialize` method on the MethodChannel.
  /// After success, the engine is in [EchoEngineState.ready].
  ///
  /// Throws [PlatformException] on failure.
  Future<void> init({required String llmPath}) async {
    print('[EchoEngine] Initializing with LLM model:');
    print('[EchoEngine]   LLM: $llmPath');

    await _methodChannel.invokeMethod('initialize', {
      'llmPath': llmPath,
    });

    _state = EchoEngineState.ready;
  }

  /// Start the interpretation pipeline with the given language pair.
  ///
  /// [srcLang] and [tgtLang] are ISO 639-1 codes (e.g. "zh", "en").
  /// The engine must be in [EchoEngineState.ready].
  ///
  /// Throws [PlatformException] on failure.
  Future<void> start({required String srcLang, required String tgtLang}) async {
    print('[EchoEngine] Starting pipeline: $srcLang -> $tgtLang');
    await _methodChannel.invokeMethod('start', {
      'srcLang': srcLang,
      'tgtLang': tgtLang,
    });
    _state = EchoEngineState.running;
  }

  /// Stop the active interpretation pipeline.
  Future<void> stop() async {
    await _methodChannel.invokeMethod('stop');
    _state = EchoEngineState.ready;
  }

  /// Dispose of all Dart-side resources.
  void dispose() {
    _eventSubscription?.cancel();
    _messageController.close();
  }

  void _onEvent(dynamic event) {
    if (event is Map<dynamic, dynamic>) {
      final message = EchoMessage.fromMap(event);
      if (message != null) {
        _messageController.add(message);

        // Auto-update state when engine reports ready
        if (message is EngineReadyMessage && _state == EchoEngineState.uninitialized) {
          _state = EchoEngineState.ready;
        }
      } else {
        print('[EchoEngine] Failed to parse event: $event');
      }
    }
  }
}
