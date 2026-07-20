import 'dart:async';

import 'package:flutter/material.dart';

import 'src/echo_engine.dart';
import 'src/messages.dart';
import 'src/model/model_catalog.dart';
import 'src/model/model_repository.dart';
import 'src/ui/model_config_screen.dart';
import 'src/ui/split_view.dart';
import 'src/ui/status_bar.dart';

void main() {
  runApp(const QwenEchoApp());
}

/// Root application widget for QwenEcho simultaneous interpretation.
class QwenEchoApp extends StatelessWidget {
  const QwenEchoApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'QwenEcho',
      theme: ThemeData.dark().copyWith(
        scaffoldBackgroundColor: Colors.black,
      ),
      home: const InterpretationScreen(),
      debugShowCheckedModeBanner: false,
    );
  }
}

/// Main interpretation screen wiring EchoEngine messages to SplitView UI.
///
/// Subscribes to [EchoEngine.messages] and routes:
/// - [AsrPartialMessage] / [AsrConfirmedMessage] → originating speaker half
/// - [TranslationStreamMessage] / [TranslationDoneMessage] → opposing half
/// - Thermal/memory/latency warnings → StatusBar overlay
///
/// Validates: Requirements 8.1, 8.2, 8.3, 8.4, 8.5, 12.1, 12.3, 12.4, 12.5
class InterpretationScreen extends StatefulWidget {
  const InterpretationScreen({super.key});

  @override
  State<InterpretationScreen> createState() => _InterpretationScreenState();
}

class _InterpretationScreenState extends State<InterpretationScreen> {
  late final EchoEngine _engine;
  StreamSubscription<EchoMessage>? _messageSubscription;

  final GlobalKey<SplitViewState> _splitViewKey = GlobalKey<SplitViewState>();

  /// Human-readable engine status shown as a small overlay.
  String _statusText = 'Initializing…';

  /// Current interpretation direction.
  String _srcLang = 'zh';
  String _tgtLang = 'en';

  @override
  void initState() {
    super.initState();
    _engine = EchoEngine();
    _messageSubscription = _engine.messages.listen(_onEngineMessage);
    _initEngine();
  }

  /// Auto-initialize the engine with bundled or imported model paths.
  Future<void> _initEngine() async {
    try {
      final repo = ModelRepository();
      final paths = await repo.resolvePathsIfComplete();
      if (paths == null) {
        setState(() => _statusText = 'Models not ready — tap settings');
        return;
      }
      await _engine.init(
        asrPath: paths[ModelKind.asr]!,
        llmPath: paths[ModelKind.llm]!,
      );
      setState(() => _statusText = 'Ready ($_srcLang→$_tgtLang) — tap mic');
    } catch (e) {
      setState(() => _statusText = 'Init failed: $e');
    }
  }

  /// Toggle the interpretation pipeline on/off.
  Future<void> _togglePipeline() async {
    if (_engine.state == EchoEngineState.uninitialized) {
      _openModelConfig();
      return;
    }
    if (_engine.state == EchoEngineState.running) {
      await _engine.stop();
      setState(() => _statusText = 'Stopped — tap mic to resume');
    } else if (_engine.state == EchoEngineState.ready) {
      try {
        await _engine.start(srcLang: _srcLang, tgtLang: _tgtLang);
        if (mounted) {
          setState(() => _statusText = 'Listening ($_srcLang→$_tgtLang)');
        }
      } catch (_) {
        // EchoEngine already posted an ErrorMessage that StatusBar will
        // render. Just revert the overlay text to something actionable.
        if (mounted) {
          setState(() => _statusText = 'Mic unavailable — check Settings');
        }
      }
    }
  }

  /// Swap source and target languages.
  /// Restarts the pipeline with the new direction if currently running.
  Future<void> _swapDirection() async {
    setState(() {
      final tmp = _srcLang;
      _srcLang = _tgtLang;
      _tgtLang = tmp;
    });
    if (_engine.state == EchoEngineState.running) {
      await _engine.stop();
      await _engine.start(srcLang: _srcLang, tgtLang: _tgtLang);
      setState(() => _statusText = 'Listening ($_srcLang→$_tgtLang)');
    } else if (_engine.state == EchoEngineState.ready) {
      setState(() => _statusText = 'Ready ($_srcLang→$_tgtLang) — tap mic');
    }
  }

  @override
  void dispose() {
    _messageSubscription?.cancel();
    _engine.dispose();
    super.dispose();
  }

  /// Route incoming engine messages to the appropriate UI component.
  void _onEngineMessage(EchoMessage message) {
    print('[UI] Message received: $message');
    final splitView = _splitViewKey.currentState;
    if (splitView == null) {
      print('[UI] SplitView is null — message dropped');
      return;
    }

    switch (message) {
      case AsrPartialMessage():
        // Partial (unconfirmed) ASR text → originating speaker's half.
        splitView.addAsrPartial(message.speakerId, message.text);

      case AsrConfirmedMessage():
        // Confirmed ASR text → originating speaker's half.
        splitView.addAsrConfirmed(message.speakerId, message.text);

      case TranslationStreamMessage():
        // Streaming translation token → opposing speaker's half.
        splitView.addTranslation(message.speakerId, message.token);

      case TranslationDoneMessage():
        // Translation complete — could finalize the line in opposing half.
        // Currently the streaming tokens already render the translation;
        // TranslationDone serves as a logical completion marker.
        break;

      case ErrorMessage():
      case ThermalStateMessage():
      case LatencyWarningMessage():
      case EngineReadyMessage():
        // These are handled by the StatusBar's own message subscription.
        break;
    }
  }

  /// Open the model configuration & management screen.
  /// Re-checks models and re-inits engine when returning.
  void _openModelConfig() {
    Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (_) => ModelConfigScreen(),
      ),
    ).then((_) {
      if (_engine.state == EchoEngineState.uninitialized) {
        _initEngine();
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      body: Stack(
        children: [
          // Full-screen bilateral split view for interpretation.
          SplitView(key: _splitViewKey),

          // Persistent status bar overlay (offline badge, thermal, warnings).
          Positioned(
            left: 0,
            right: 0,
            top: 0,
            child: StatusBar(messages: _engine.messages),
          ),

          // Language direction indicator + swap button (centered on divider).
          Align(
            alignment: Alignment.center,
            child: GestureDetector(
              onTap: _swapDirection,
              child: Container(
                padding:
                    const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
                decoration: BoxDecoration(
                  color: const Color(0xE61A1A1A),
                  borderRadius: BorderRadius.circular(20),
                  border: Border.all(
                      color: const Color(0xFF00E676), width: 1),
                ),
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Text(
                      '${_srcLang.toUpperCase()} → ${_tgtLang.toUpperCase()}',
                      style: const TextStyle(
                        color: Colors.white,
                        fontSize: 12,
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                    const SizedBox(width: 4),
                    const Icon(Icons.swap_horiz,
                        color: Color(0xFF00E676), size: 18),
                  ],
                ),
              ),
            ),
          ),

          // Engine status text overlay.
          Positioned(
            left: 0,
            right: 0,
            bottom: 88,
            child: Center(
              child: Container(
                padding:
                    const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
                decoration: BoxDecoration(
                  color: const Color(0x99000000),
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Text(
                  _statusText,
                  style: const TextStyle(color: Colors.white70, fontSize: 12),
                ),
              ),
            ),
          ),

          // Model configuration button (top-right, below status bar).
          Positioned(
            right: 8,
            top: 52,
            child: SafeArea(
              child: Material(
                color: const Color(0xCC1A1A1A),
                shape: const CircleBorder(),
                child: IconButton(
                  tooltip: 'Model configuration',
                  icon: const Icon(Icons.settings,
                      color: Colors.white70, size: 20),
                  onPressed: _openModelConfig,
                ),
              ),
            ),
          ),

          // Mic toggle button (bottom-right).
          Positioned(
            right: 16,
            bottom: 16,
            child: SafeArea(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  // Debug test-inject button (only visible when running).
                  if (_engine.state == EchoEngineState.running)
                    Padding(
                      padding: const EdgeInsets.only(bottom: 12),
                      child: FloatingActionButton.small(
                        heroTag: 'test_inject',
                        backgroundColor: const Color(0xFF2196F3),
                        tooltip: 'Inject test text',
                        onPressed: () {
                          _engine.testInject('你好世界，这是一个语音翻译测试。');
                        },
                        child: const Icon(Icons.bug_report, color: Colors.white),
                      ),
                    ),
                  FloatingActionButton(
                    backgroundColor:
                        _engine.state == EchoEngineState.running
                            ? const Color(0xFFFF5252)
                            : _engine.state == EchoEngineState.ready
                                ? const Color(0xFF00E676)
                                : const Color(0xFF616161),
                    onPressed: _togglePipeline,
                    child: Icon(
                      _engine.state == EchoEngineState.running
                          ? Icons.stop
                          : Icons.mic,
                      color: Colors.white,
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}
