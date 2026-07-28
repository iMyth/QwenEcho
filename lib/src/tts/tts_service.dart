/// Dart-side wrapper around the Swift AVSpeechSynthesizer plugin.
///
/// Exposes a simple `speak(text, lang)` and `stop()` API. The Swift side does
/// all the heavy lifting — voice selection, utterance queueing, audio session
/// integration. This class just bridges MethodChannel calls.
///
/// Designed to be called after a TranslationDoneMessage arrives, so the
/// opposing speaker hears the translation spoken aloud.
library;

import 'package:flutter/services.dart';
import 'package:flutter/foundation.dart';

/// Exception thrown when the TTS plugin returns an error.
class TtsException implements Exception {
  final String message;
  TtsException(this.message);
  @override
  String toString() => 'TtsException: $message';
}

/// Text-to-speech service backed by iOS AVSpeechSynthesizer.
class TtsService {
  static const _channel = MethodChannel('qwen_echo_tts');

  bool _enabled = true;

  /// Whether TTS playback is enabled. Set to false to mute output without
  /// disposing the service (e.g. user toggles "mute speaker" in UI).
  bool get enabled => _enabled;
  set enabled(bool value) {
    _enabled = value;
    if (!value) {
      // Stop any in-flight utterance when the user mutes.
      stop();
    }
  }

  /// Speak [text] in the given language.
  ///
  /// [lang] should be an ISO 639-1 code ("en", "zh", "ja", …). The Swift
  /// side maps it to the best available system voice.
  ///
  /// No-op when [enabled] is false.
  Future<void> speak(String text, {required String lang}) async {
    if (!_enabled) return;
    if (text.trim().isEmpty) return;

    try {
      await _channel.invokeMethod('speak', {
        'text': text,
        'lang': lang,
      });
    } on PlatformException catch (e) {
      // Non-fatal — log and continue. The translation is still visible on
      // screen, so failed audio shouldn't crash the interpretation flow.
      debugPrint('[TtsService] speak failed: ${e.code} — ${e.message}');
    }
  }

  /// Stop any in-flight utterance immediately.
  Future<void> stop() async {
    try {
      await _channel.invokeMethod('stop');
    } on PlatformException catch (e) {
      debugPrint('[TtsService] stop failed: ${e.code}');
    }
  }

  /// List the system voices available (for diagnostics / settings UI).
  Future<List<Map<dynamic, dynamic>>> getVoices() async {
    try {
      final result = await _channel.invokeMethod('getVoices');
      if (result is List) {
        return result.cast<Map<dynamic, dynamic>>();
      }
      return [];
    } on PlatformException catch (e) {
      debugPrint('[TtsService] getVoices failed: ${e.code}');
      return [];
    }
  }
}
