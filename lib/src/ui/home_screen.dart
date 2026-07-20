/// Home / setup screen — the entry point users see on launch.
///
/// Responsibilities:
/// - Verify that the required ASR + LLM models are present in the sandbox
/// - Let the user pick source and target languages
/// - Initialize the engine once (loads models into memory)
/// - Hand off to [InterpretationScreen] when the user taps "Start"
///
/// The engine is created here and lives for the lifetime of this screen.
/// Navigating to the interpretation screen passes the engine down; returning
/// to home does NOT tear it down, so the user can stop and restart quickly.
library;

import 'dart:async';

import 'package:flutter/material.dart';

import '../echo_engine.dart';
import '../messages.dart';
import '../model/model_catalog.dart';
import '../model/model_repository.dart';
import '../tts/tts_service.dart';
import 'languages.dart';
import 'model_config_screen.dart';
import 'split_view.dart';
import 'status_bar.dart';

/// Landing page for QwenEcho.
class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  static const Color _accent = Color(0xFF00E676);

  final ModelRepository _repository = ModelRepository();

  EchoEngine? _engine;
  bool _engineInitializing = false;
  String? _engineError;

  List<ModelStatus>? _modelStatuses;
  bool _modelsLoading = true;

  SupportedLanguage _srcLang = kSupportedLanguages.first; // Chinese
  SupportedLanguage _tgtLang = kSupportedLanguages[1]; // English

  @override
  void initState() {
    super.initState();
    _refreshModels();
  }

  @override
  void dispose() {
    // Engine is torn down only when home is disposed (app backgrounded /
    // user exits). Interpretation screen does NOT dispose it.
    _engine?.dispose();
    super.dispose();
  }

  Future<void> _refreshModels() async {
    setState(() {
      _modelsLoading = true;
    });
    final statuses = await _repository.statusAll();
    if (!mounted) return;
    setState(() {
      _modelStatuses = statuses;
      _modelsLoading = false;
    });
  }

  bool get _modelsReady =>
      _modelStatuses != null && _modelStatuses!.every((s) => s.isReady);

  int get _readyCount =>
      _modelStatuses?.where((s) => s.isReady).length ?? 0;

  int get _totalCount => _modelStatuses?.length ?? 0;

  /// Initialize the engine with the resolved model paths.
  ///
  /// Called once when the user taps "Start". Subsequent taps while the engine
  /// is already initialized skip straight to the interpretation screen.
  Future<void> _ensureEngineInitialized() async {
    if (_engine != null) return;
    if (_engineInitializing) return;

    setState(() {
      _engineInitializing = true;
      _engineError = null;
    });

    final paths = await _repository.resolvePathsIfComplete();
    if (paths == null) {
      setState(() {
        _engineInitializing = false;
        _engineError = 'Models not ready — open Model Settings to import.';
      });
      return;
    }

    final engine = EchoEngine();
    try {
      await engine.init(
        asrPath: paths[ModelKind.asr]!,
        llmPath: paths[ModelKind.llm]!,
      );
      if (!mounted) {
        await engine.dispose();
        return;
      }
      setState(() {
        _engine = engine;
        _engineInitializing = false;
      });
    } catch (e) {
      print('[HomeScreen] engine init failed: $e');
      await engine.dispose();
      if (!mounted) return;
      setState(() {
        _engineInitializing = false;
        _engineError = 'Engine init failed: $e';
      });
    }
  }

  void _openModelSettings() {
    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (_) => ModelConfigScreen(repository: _repository),
      ),
    ).then((_) => _refreshModels());
  }

  Future<void> _startInterpretation() async {
    await _ensureEngineInitialized();
    final engine = _engine;
    if (engine == null) return;

    if (!mounted) return;
    await Navigator.push(
      context,
      MaterialPageRoute(
        builder: (_) => InterpretationRoute(
          engine: engine,
          srcLang: _srcLang,
          tgtLang: _tgtLang,
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      appBar: AppBar(
        backgroundColor: Colors.black,
        foregroundColor: Colors.white,
        elevation: 0,
        title: const Text('QwenEcho'),
        actions: [
          IconButton(
            tooltip: 'Model Settings',
            icon: const Icon(Icons.storage_outlined),
            onPressed: _openModelSettings,
          ),
        ],
      ),
      body: _modelsLoading
          ? const Center(child: CircularProgressIndicator(color: _accent))
          : ListView(
              padding: const EdgeInsets.all(20),
              children: [
                _buildHeader(),
                const SizedBox(height: 20),
                _buildModelStatusCard(),
                const SizedBox(height: 24),
                _buildLanguagePicker(),
                const SizedBox(height: 32),
                _buildStartButton(),
                if (_engineError != null) ...[
                  const SizedBox(height: 16),
                  _buildErrorBanner(),
                ],
              ],
            ),
    );
  }

  Widget _buildHeader() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text(
          'Simultaneous Interpretation',
          style: TextStyle(
            color: Colors.white,
            fontSize: 22,
            fontWeight: FontWeight.w700,
          ),
        ),
        const SizedBox(height: 6),
        const Text(
          'On-device · Air-gapped · No network',
          style: TextStyle(color: Color(0xFF9E9E9E), fontSize: 13),
        ),
        const SizedBox(height: 12),
        const Text(
          'Place the phone between two speakers. The top half is flipped for '
          'the person across from you.',
          style: TextStyle(color: Color(0xFFBDBDBD), fontSize: 13),
        ),
      ],
    );
  }

  Widget _buildModelStatusCard() {
    final ready = _modelsReady;
    final color = ready ? _accent : const Color(0xFFFFB300);
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: const Color(0xFF1A1A1A),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: color.withOpacity(0.5)),
      ),
      child: Row(
        children: [
          Icon(
            ready ? Icons.verified : Icons.warning_amber_rounded,
            color: color,
            size: 26,
          ),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  ready
                      ? 'All models ready'
                      : 'Models ready: $_readyCount / $_totalCount',
                  style: TextStyle(
                    color: Colors.white,
                    fontSize: 15,
                    fontWeight: FontWeight.w600,
                  ),
                ),
                const SizedBox(height: 2),
                Text(
                  ready
                      ? 'SenseVoice ASR + Qwen3.5 LLM loaded locally'
                      : 'Open Model Settings to import missing models',
                  style: const TextStyle(color: Color(0xFF9E9E9E), fontSize: 12),
                ),
              ],
            ),
          ),
          TextButton(
            onPressed: _openModelSettings,
            child: const Text('Settings', style: TextStyle(color: _accent)),
          ),
        ],
      ),
    );
  }

  Widget _buildLanguagePicker() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text(
          'Language Pair',
          style: TextStyle(
            color: Colors.white,
            fontSize: 15,
            fontWeight: FontWeight.w600,
          ),
        ),
        const SizedBox(height: 12),
        _LanguageDropdown(
          label: 'You speak',
          value: _srcLang,
          accent: _accent,
          onChanged: (lang) {
            if (lang == null) return;
            // Prevent same-language pair.
            if (lang.code == _tgtLang.code) {
              setState(() => _tgtLang = _srcLang);
            }
            setState(() => _srcLang = lang);
          },
        ),
        const SizedBox(height: 12),
        Center(
          child: IconButton(
            tooltip: 'Swap languages',
            icon: const Icon(Icons.swap_vert, color: _accent, size: 28),
            onPressed: () {
              setState(() {
                final tmp = _srcLang;
                _srcLang = _tgtLang;
                _tgtLang = tmp;
              });
            },
          ),
        ),
        const SizedBox(height: 4),
        _LanguageDropdown(
          label: 'Translate to',
          value: _tgtLang,
          accent: _accent,
          onChanged: (lang) {
            if (lang == null) return;
            if (lang.code == _srcLang.code) {
              setState(() => _srcLang = _tgtLang);
            }
            setState(() => _tgtLang = lang);
          },
        ),
      ],
    );
  }

  Widget _buildStartButton() {
    final enabled = _modelsReady && !_engineInitializing;
    return SizedBox(
      height: 56,
      child: ElevatedButton(
        onPressed: enabled ? _startInterpretation : null,
        style: ElevatedButton.styleFrom(
          backgroundColor: _accent,
          foregroundColor: Colors.black,
          disabledBackgroundColor: const Color(0xFF2A2A2A),
          disabledForegroundColor: const Color(0xFF616161),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(12),
          ),
          textStyle: const TextStyle(
            fontSize: 16,
            fontWeight: FontWeight.w700,
          ),
        ),
        child: _engineInitializing
            ? const Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  SizedBox(
                    width: 18,
                    height: 18,
                    child: CircularProgressIndicator(
                      strokeWidth: 2,
                      color: Color(0xFF616161),
                    ),
                  ),
                  SizedBox(width: 12),
                  Text('Loading models…'),
                ],
              )
            : const Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Icon(Icons.mic_rounded),
                  SizedBox(width: 8),
                  Text('Start Interpreting'),
                ],
              ),
      ),
    );
  }

  Widget _buildErrorBanner() {
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: const Color(0xFF3A1515),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: const Color(0xFFFF5252).withOpacity(0.4)),
      ),
      child: Row(
        children: [
          const Icon(Icons.error_outline, color: Color(0xFFFF5252), size: 20),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              _engineError!,
              style: const TextStyle(color: Color(0xFFFF8A80), fontSize: 13),
            ),
          ),
        ],
      ),
    );
  }
}

/// A styled language dropdown.
class _LanguageDropdown extends StatelessWidget {
  final String label;
  final SupportedLanguage value;
  final Color accent;
  final ValueChanged<SupportedLanguage?> onChanged;

  const _LanguageDropdown({
    required this.label,
    required this.value,
    required this.accent,
    required this.onChanged,
  });

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          label,
          style: const TextStyle(color: Color(0xFF9E9E9E), fontSize: 12),
        ),
        const SizedBox(height: 6),
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 4),
          decoration: BoxDecoration(
            color: const Color(0xFF1A1A1A),
            borderRadius: BorderRadius.circular(10),
            border: Border.all(color: const Color(0xFF2A2A2A)),
          ),
          child: DropdownButton<SupportedLanguage>(
            value: value,
            isExpanded: true,
            underline: const SizedBox.shrink(),
            dropdownColor: const Color(0xFF1A1A1A),
            icon: Icon(Icons.arrow_drop_down, color: accent),
            style: const TextStyle(color: Colors.white, fontSize: 15),
            items: [
              for (final lang in kSupportedLanguages)
                DropdownMenuItem<SupportedLanguage>(
                  value: lang,
                  child: Text(lang.pickerLabel),
                ),
            ],
            onChanged: onChanged,
          ),
        ),
      ],
    );
  }
}

/// Route that wraps [InterpretationScreen] and carries the engine + languages.
///
/// We can't pass these through `MaterialApp.initialRoute` arguments cleanly,
/// so this is a thin widget that takes them as constructor parameters and
/// builds the real screen.
class InterpretationRoute extends StatelessWidget {
  final EchoEngine engine;
  final SupportedLanguage srcLang;
  final SupportedLanguage tgtLang;

  const InterpretationRoute({
    super.key,
    required this.engine,
    required this.srcLang,
    required this.tgtLang,
  });

  @override
  Widget build(BuildContext context) {
    return InterpretationScreen(
      engine: engine,
      srcLang: srcLang,
      tgtLang: tgtLang,
    );
  }
}

/// Interpretation screen — the full-screen bilateral split view with
/// start/stop control and status overlay.
///
/// Takes an already-initialized [EchoEngine] so that returning to home and
/// re-entering does not reload models.
class InterpretationScreen extends StatefulWidget {
  final EchoEngine engine;
  final SupportedLanguage srcLang;
  final SupportedLanguage tgtLang;

  const InterpretationScreen({
    super.key,
    required this.engine,
    required this.srcLang,
    required this.tgtLang,
  });

  @override
  State<InterpretationScreen> createState() => _InterpretationScreenState();
}

class _InterpretationScreenState extends State<InterpretationScreen> {
  StreamSubscription<EchoMessage>? _messageSubscription;
  final GlobalKey<SplitViewState> _splitViewKey = GlobalKey<SplitViewState>();

  /// TTS playback service (bridges to iOS AVSpeechSynthesizer).
  final TtsService _ttsService = TtsService();

  bool _isRunning = false;
  bool _isStarting = false;
  String? _lastError;

  /// Whether TTS output is muted. When muted, the translation is still
  /// displayed but not spoken aloud.
  bool _ttsMuted = false;

  @override
  void initState() {
    super.initState();
    _messageSubscription = widget.engine.messages.listen(_onEngineMessage);
    // Auto-start the pipeline when entering the interpretation screen so the
    // user doesn't have to tap a button immediately after a loading screen.
    _startPipeline();
  }

  @override
  void dispose() {
    _messageSubscription?.cancel();
    // Stop but do NOT dispose the engine — home still owns it.
    if (_isRunning) {
      widget.engine.stop();
    }
    _ttsService.stop();
    super.dispose();
  }

  void _onEngineMessage(EchoMessage message) {
    final splitView = _splitViewKey.currentState;
    if (splitView == null) return;

    switch (message) {
      case AsrPartialMessage():
        splitView.addAsrPartial(message.speakerId, message.text);

      case AsrConfirmedMessage():
        splitView.addAsrConfirmed(message.speakerId, message.text);

      case TranslationStreamMessage():
        splitView.addTranslation(message.speakerId, message.token);

      case TranslationDoneMessage():
        // Translation complete — speak the full text aloud so the opposing
        // speaker can hear it. The streaming tokens already rendered the
        // text on screen; TTS is the audible mirror for face-to-face use.
        if (!_ttsMuted && message.text.isNotEmpty) {
          _ttsService.speak(message.text, lang: widget.tgtLang.code);
        }
        break;

      case ErrorMessage():
        setState(() => _lastError = message.detail);

      case ThermalStateMessage():
      case LatencyWarningMessage():
      case EngineReadyMessage():
      case TtsStartedMessage():
      case TtsCompleteMessage():
        // Handled by the StatusBar overlay or ignored (informational).
        break;
    }
  }

  void _toggleTtsMute() {
    setState(() => _ttsMuted = !_ttsMuted);
    _ttsService.enabled = !_ttsMuted;
  }

  Future<void> _startPipeline() async {
    if (_isStarting || _isRunning) return;
    setState(() {
      _isStarting = true;
      _lastError = null;
    });
    try {
      await widget.engine.start(
        srcLang: widget.srcLang.code,
        tgtLang: widget.tgtLang.code,
      );
      if (!mounted) return;
      setState(() {
        _isStarting = false;
        _isRunning = true;
      });
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _isStarting = false;
        _lastError = 'Start failed: $e';
      });
    }
  }

  Future<void> _stopPipeline() async {
    if (!_isRunning) return;
    setState(() => _isStarting = true);
    try {
      await widget.engine.stop();
    } catch (e) {
      print('[InterpretationScreen] stop failed: $e');
    }
    if (!mounted) return;
    setState(() {
      _isStarting = false;
      _isRunning = false;
    });
  }

  Future<void> _togglePipeline() async {
    if (_isRunning) {
      await _stopPipeline();
    } else {
      await _startPipeline();
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
            child: StatusBar(messages: widget.engine.messages),
          ),

          // Language pair indicator + central control.
          Positioned(
            left: 0,
            right: 0,
            bottom: 0,
            child: _ControlBar(
              srcLang: widget.srcLang,
              tgtLang: widget.tgtLang,
              isRunning: _isRunning,
              isStarting: _isStarting,
              ttsMuted: _ttsMuted,
              onToggle: _togglePipeline,
              onToggleMute: _toggleTtsMute,
              onExit: () => Navigator.pop(context),
            ),
          ),

          // Error toast.
          if (_lastError != null)
            Positioned(
              left: 16,
              right: 16,
              bottom: 120,
              child: _ErrorBanner(
                text: _lastError!,
                onDismiss: () => setState(() => _lastError = null),
              ),
            ),
        ],
      ),
    );
  }
}

/// Bottom control bar showing the language pair and start/stop button.
class _ControlBar extends StatelessWidget {
  final SupportedLanguage srcLang;
  final SupportedLanguage tgtLang;
  final bool isRunning;
  final bool isStarting;
  final bool ttsMuted;
  final VoidCallback onToggle;
  final VoidCallback onToggleMute;
  final VoidCallback onExit;

  const _ControlBar({
    required this.srcLang,
    required this.tgtLang,
    required this.isRunning,
    required this.isStarting,
    required this.ttsMuted,
    required this.onToggle,
    required this.onToggleMute,
    required this.onExit,
  });

  @override
  Widget build(BuildContext context) {
    const accent = Color(0xFF00E676);
    return Container(
      padding: const EdgeInsets.fromLTRB(16, 12, 16, 24),
      decoration: const BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topCenter,
          end: Alignment.bottomCenter,
          colors: [Colors.transparent, Color(0xCC000000)],
        ),
      ),
      child: Row(
        children: [
          // Back button
          IconButton(
            tooltip: 'Exit',
            icon: const Icon(Icons.arrow_back, color: Colors.white),
            onPressed: onExit,
          ),
          const SizedBox(width: 8),
          // Language pair label
          Expanded(
            child: Text(
              '${srcLang.flag} ${srcLang.code.toUpperCase()} → '
              '${tgtLang.flag} ${tgtLang.code.toUpperCase()}',
              style: const TextStyle(
                color: Colors.white,
                fontSize: 14,
                fontWeight: FontWeight.w500,
              ),
            ),
          ),
          // Mute / unmute button
          IconButton(
            tooltip: ttsMuted ? 'Unmute speaker' : 'Mute speaker',
            icon: Icon(
              ttsMuted ? Icons.volume_off : Icons.volume_up,
              color: ttsMuted ? const Color(0xFF9E9E9E) : Colors.white,
            ),
            onPressed: onToggleMute,
          ),
          // Start/stop button
          SizedBox(
            width: 64,
            height: 64,
            child: Stack(
              alignment: Alignment.center,
              children: [
                if (isStarting)
                  const CircularProgressIndicator(
                    strokeWidth: 2,
                    color: accent,
                  ),
                Material(
                  color: isRunning
                      ? const Color(0xFFFF5252)
                      : accent,
                  shape: const CircleBorder(),
                  elevation: 4,
                  child: InkWell(
                    customBorder: const CircleBorder(),
                    onTap: isStarting ? null : onToggle,
                    child: SizedBox(
                      width: 56,
                      height: 56,
                      child: Icon(
                        isRunning ? Icons.stop : Icons.mic_rounded,
                        color: isRunning ? Colors.white : Colors.black,
                        size: 28,
                      ),
                    ),
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _ErrorBanner extends StatelessWidget {
  final String text;
  final VoidCallback onDismiss;

  const _ErrorBanner({required this.text, required this.onDismiss});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: const Color(0xEE3A1515),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: const Color(0xFFFF5252).withOpacity(0.5)),
      ),
      child: Row(
        children: [
          const Icon(Icons.error_outline, color: Color(0xFFFF5252), size: 20),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              text,
              style: const TextStyle(color: Color(0xFFFF8A80), fontSize: 13),
            ),
          ),
          IconButton(
            icon: const Icon(Icons.close, color: Color(0xFFFF8A80), size: 18),
            onPressed: onDismiss,
            padding: EdgeInsets.zero,
            constraints: const BoxConstraints(),
          ),
        ],
      ),
    );
  }
}
