import 'dart:async';

import 'package:flutter/material.dart';

import 'src/echo_engine.dart';
import 'src/messages.dart';
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

  @override
  void initState() {
    super.initState();
    _engine = EchoEngine();
    _messageSubscription = _engine.messages.listen(_onEngineMessage);
  }

  @override
  void dispose() {
    _messageSubscription?.cancel();
    _engine.dispose();
    super.dispose();
  }

  /// Route incoming engine messages to the appropriate UI component.
  void _onEngineMessage(EchoMessage message) {
    final splitView = _splitViewKey.currentState;
    if (splitView == null) return;

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

      case TtsStartedMessage():
      case TtsCompleteMessage():
        // TTS lifecycle events — no direct UI rendering needed.
        // Audio output is handled natively via HAL.
        break;

      case ErrorMessage():
      case ThermalStateMessage():
      case MemoryWarningMessage():
      case LatencyWarningMessage():
      case SampleDropMessage():
        // These are handled by the StatusBar's own message subscription.
        break;
    }
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
        ],
      ),
    );
  }
}
