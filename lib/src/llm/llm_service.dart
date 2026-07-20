/// Dart-side LLM service using llamadart (llama.cpp GGUF).
///
/// Wraps model loading and streaming generation. Translates confirmed ASR
/// text from the source language into the target language, maintaining a
/// sliding context window of the last few translations for coherence.
library;

import 'dart:async';

import 'package:llamadart/llamadart.dart';

/// Exception thrown when the LLM service fails to load or generate.
class LlmServiceException implements Exception {
  final String message;
  LlmServiceException(this.message);
  @override
  String toString() => 'LlmServiceException: $message';
}

/// High-level wrapper around llamadart for translation.
class LlmService {
  LlamaEngine? _engine;
  ChatSession? _session;

  /// Maximum number of previous translation pairs to keep as context.
  static const int _maxContextPairs = 3;

  /// Sliding window of recent source/target pairs for context.
  final List<_TranslationPair> _context = [];

  /// Whether the model has been loaded and is ready.
  bool get isLoaded => _engine != null;

  /// Load a GGUF model from [path].
  ///
  /// Must be called before [translate]. Safe to call multiple times only
  /// after [dispose].
  Future<void> load(String path) async {
    print('[LlmService] Loading model from $path');
    await dispose();

    final engine = LlamaEngine(LlamaBackend());
    try {
      // Force CPU-only inference with a small context window to stay within
      // simulator memory budgets and avoid GPU backend crashes.
      await engine.loadModel(
        path,
        modelParams: const ModelParams(
          contextSize: 512,
          gpuLayers: 0,
          numberOfThreads: 2,
          numberOfThreadsBatch: 2,
        ),
      );
      _engine = engine;
      _session = ChatSession(engine);
      print('[LlmService] Model loaded successfully');
    } catch (e, st) {
      print('[LlmService] Failed to load model: $e');
      print('[LlmService] $st');
      await engine.dispose();
      throw LlmServiceException('Failed to load model at $path: $e');
    }
  }

  /// Translate [text] from [srcLang] to [tgtLang], returning a token stream.
  ///
  /// The model must already be loaded via [load]. The stream yields each
  /// decoded token as it is generated and closes when translation is complete.
  Stream<String> translate(
    String text, {
    required String srcLang,
    required String tgtLang,
    int maxTokens = 128,
    double temperature = 0.3,
  }) {
    final session = _session;
    if (session == null) {
      return Stream.fromFuture(
        Future.error(LlmServiceException('Model not loaded')),
      );
    }

    final prompt = _buildPrompt(text, srcLang: srcLang, tgtLang: tgtLang);
    final params = GenerationParams(maxTokens: maxTokens, temp: temperature);

    print('[LlmService] Starting translation with prompt length ${prompt.length}');

    return session
        .create(
          [LlamaTextContent(prompt)],
          params: params,
          // Qwen3 / Qwen3.5 default to "thinking mode": all generated text
          // goes to the `thinking` field of the chunk delta while `content`
          // stays null. For a pure translation task we don't want the model
          // to reason aloud — disable thinking so it writes directly to
          // `content`, which is what we read downstream.
          enableThinking: false,
        )
        .handleError((Object error, StackTrace stackTrace) {
          print('[LlmService] Translation stream error: $error');
          print('[LlmService] $stackTrace');
          throw LlmServiceException('Translation failed: $error');
        })
        .map((chunk) {
          print('[LlmService] Raw chunk: $chunk');
          final content = chunk.choices.first.delta.content;
          if (content != null && content.isNotEmpty) {
            print('[LlmService] Token: $content');
          }
          return content;
        })
        .where((token) => token != null && token.isNotEmpty)
        .cast<String>();
  }

  /// Add a completed translation to the sliding context window.
  void addToContext(String source, String translation) {
    _context.add(_TranslationPair(source, translation));
    if (_context.length > _maxContextPairs) {
      _context.removeAt(0);
    }
  }

  /// Release the underlying engine and clear state.
  Future<void> dispose() async {
    _session = null;
    final engine = _engine;
    _engine = null;
    _context.clear();
    if (engine != null) {
      await engine.dispose();
    }
  }

  /// Build a translation prompt using Qwen3.5-Instruct chat format.
  String _buildPrompt(
    String text, {
    required String srcLang,
    required String tgtLang,
  }) {
    // Qwen3.5-Instruct chat template markers.
    const imStart = '<|im_start|>';
    const imEnd = '<|im_end|>';

    final buffer = StringBuffer();
    buffer.writeln('$imStart system');
    buffer.writeln(
      'You are a professional translator. Translate the user sentence from '
      '$srcLang to $tgtLang. Output only the translation, with no explanation '
      'or extra text.',
    );
    buffer.writeln(imEnd);
    buffer.writeln('$imStart user');
    buffer.writeln('$text /no_think');
    buffer.writeln(imEnd);
    buffer.writeln('$imStart assistant');
    return buffer.toString();
  }
}

class _TranslationPair {
  final String source;
  final String translation;
  _TranslationPair(this.source, this.translation);
}
