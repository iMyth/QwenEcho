/// High-level QwenEcho engine API.
///
/// Bridges the Swift native engine (ASR/audio via MethodChannel/EventChannel)
/// with the Dart-side LLM service (llamadart). Manages the init -> start -> stop
/// lifecycle.
library;

import 'dart:async';

import 'package:flutter/services.dart';

import 'llm/llm_service.dart';
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

/// Facade over the native QwenEcho engine (Swift via MethodChannel) and the
/// Dart-side LLM service (llamadart).
///
/// Usage:
/// ```dart
/// final engine = EchoEngine();
/// engine.messages.listen((msg) { /* handle */ });
/// engine.init(asrPath: '...', llmPath: '...');
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

  final LlmService _llmService = LlmService();

  final StreamController<EchoMessage> _messageController =
      StreamController<EchoMessage>.broadcast();

  /// Stream of typed messages from the engine.
  Stream<EchoMessage> get messages => _messageController.stream;

  StreamSubscription<dynamic>? _eventSubscription;

  String _srcLang = 'zh';
  String _tgtLang = 'en';

  /// Create an [EchoEngine].
  EchoEngine() {
    _eventSubscription = _eventChannel.receiveBroadcastStream().listen(
      _onEvent,
      onError: (error) {
        print('[EchoEngine] EventChannel error: $error');
      },
    );
  }

  /// Initialize the engine with ASR and LLM model paths.
  ///
  /// [asrPath] is the sherpa-onnx model package directory.
  /// [llmPath] is the GGUF file path for llamadart.
  /// [ttsPath] is reserved for Phase 5 and ignored for now.
  ///
  /// After success, the engine is in [EchoEngineState.ready].
  ///
  /// Throws [PlatformException] on failure.
  Future<void> init({
    required String asrPath,
    required String llmPath,
    String? ttsPath,
  }) async {
    print('[EchoEngine] Initializing with models:');
    print('[EchoEngine]   ASR: $asrPath');
    print('[EchoEngine]   LLM: $llmPath');

    await _methodChannel.invokeMethod('initialize', {
      'asrPath': asrPath,
      'llmPath': llmPath,
      if (ttsPath != null) 'ttsPath': ttsPath,
    });

    await _llmService.load(llmPath);

    _state = EchoEngineState.ready;
  }

  /// Start the interpretation pipeline with the given language pair.
  ///
  /// [srcLang] and [tgtLang] are ISO 639-1 codes (e.g. "zh", "en").
  /// The engine must be in [EchoEngineState.ready].
  ///
  /// Throws [PlatformException] on failure — common cases:
  /// - `start_failed` / `Microphone permission denied` — user refused the
  ///   iOS microphone permission prompt (or previously denied it).
  /// - `start_failed` / `Audio session setup failed` — audio session could
  ///   not be configured (rare; another app may own the audio session).
  /// - `start_failed` / `Audio capture failed` — AVAudioEngine could not
  ///   start (device mic unavailable).
  Future<void> start({required String srcLang, required String tgtLang}) async {
    print('[EchoEngine] Starting pipeline: $srcLang -> $tgtLang');
    _srcLang = srcLang;
    _tgtLang = tgtLang;
    try {
      await _methodChannel.invokeMethod('start', {
        'srcLang': srcLang,
        'tgtLang': tgtLang,
      });
      _state = EchoEngineState.running;
    } on PlatformException catch (e) {
      // Leave state as `ready` so the user can retry after fixing the
      // underlying issue (e.g. enabling mic in Settings).
      print('[EchoEngine] start failed: ${e.code} — ${e.message}');
      _messageController.add(ErrorMessage(
        code: -7,
        detail: 'Start failed: ${e.message ?? e.code}',
      ));
      rethrow;
    }
  }

  /// Stop the active interpretation pipeline.
  Future<void> stop() async {
    await _methodChannel.invokeMethod('stop');
    _state = EchoEngineState.ready;
  }

  /// Inject test text for simulator debugging (no microphone required).
  ///
  /// Posts a fake ASR-confirmed segment through the native side, which
  /// triggers the LLM translation pipeline and UI display.
  Future<void> testInject(String text, {int speakerId = 0}) async {
    print('[EchoEngine] Injecting test text: $text');
    try {
      await _methodChannel.invokeMethod('test_inject', {
        'text': text,
        'speakerId': speakerId,
      });
      print('[EchoEngine] Test inject completed');
    } catch (e, st) {
      print('[EchoEngine] Test inject failed: $e');
      print('[EchoEngine] $st');
      rethrow;
    }
  }

  /// Dispose of all Dart-side resources.
  Future<void> dispose() async {
    _eventSubscription?.cancel();
    _messageController.close();
    await _llmService.dispose();
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

        // Route confirmed ASR text through the Dart-side LLM service.
        if (message is AsrConfirmedMessage) {
          _runTranslation(message);
        }
      } else {
        print('[EchoEngine] Failed to parse event: $event');
      }
    }
  }

  void _runTranslation(AsrConfirmedMessage asr) {
    final speakerId = asr.speakerId;
    final segmentId = asr.segmentId;
    final sourceText = asr.text;

    print('[EchoEngine] Running translation for segment $segmentId: $sourceText');

    String buffer = '';
    _llmService
        .translate(
      sourceText,
      srcLang: _srcLang,
      tgtLang: _tgtLang,
    )
        .timeout(
      const Duration(seconds: 30),
      onTimeout: (sink) {
        print('[EchoEngine] Translation timed out');
        sink.close();
        _messageController.add(ErrorMessage(
          code: -6,
          detail: 'Translation timed out',
        ));
      },
    )
        .listen(
      (token) {
        buffer += token;
        _messageController.add(TranslationStreamMessage(
          speakerId: speakerId,
          token: token,
          segmentId: segmentId,
        ));
      },
      onDone: () {
        print('[EchoEngine] Translation done for segment $segmentId: $buffer');
        _llmService.addToContext(sourceText, buffer);
        _messageController.add(TranslationDoneMessage(
          speakerId: speakerId,
          text: buffer,
          segmentId: segmentId,
        ));
      },
      onError: (error) {
        print('[EchoEngine] Translation failed: $error');
        _messageController.add(ErrorMessage(
          code: -5,
          detail: 'Translation failed: $error',
        ));
      },
    );
  }
}
