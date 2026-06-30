/// Message type definitions for Native Port communication.
///
/// All messages from the C/C++ Engine arrive as typed Lists via Native Port.
/// Each message begins with an integer type tag followed by payload fields.
library;

/// Base class for all messages received from the native engine.
sealed class EchoMessage {
  const EchoMessage();

  /// Parse a raw Native Port message list into a typed [EchoMessage].
  ///
  /// Returns `null` if the message format is unrecognized.
  static EchoMessage? fromRawList(List<dynamic> raw) {
    if (raw.isEmpty) return null;

    final int typeTag = raw[0] as int;

    return switch (typeTag) {
      MessageType.asrPartial => AsrPartialMessage._fromRaw(raw),
      MessageType.asrConfirmed => AsrConfirmedMessage._fromRaw(raw),
      MessageType.translationStream => TranslationStreamMessage._fromRaw(raw),
      MessageType.translationDone => TranslationDoneMessage._fromRaw(raw),
      MessageType.ttsStarted => TtsStartedMessage._fromRaw(raw),
      MessageType.ttsComplete => TtsCompleteMessage._fromRaw(raw),
      MessageType.error => ErrorMessage._fromRaw(raw),
      MessageType.thermalState => ThermalStateMessage._fromRaw(raw),
      MessageType.memoryWarning => MemoryWarningMessage._fromRaw(raw),
      MessageType.latencyWarning => LatencyWarningMessage._fromRaw(raw),
      MessageType.sampleDrop => SampleDropMessage._fromRaw(raw),
      _ => null,
    };
  }
}

/// Native Port message type tags matching the C enum MessageType.
abstract final class MessageType {
  static const int asrPartial = 1;
  static const int asrConfirmed = 2;
  static const int translationStream = 3;
  static const int translationDone = 4;
  static const int ttsStarted = 5;
  static const int ttsComplete = 6;
  static const int error = 10;
  static const int thermalState = 11;
  static const int memoryWarning = 12;
  static const int latencyWarning = 13;
  static const int sampleDrop = 14;
}

/// [1, speaker_id, text, timestamp_ms] — Temporary/unconfirmed ASR text.
final class AsrPartialMessage extends EchoMessage {
  final int speakerId;
  final String text;
  final int timestampMs;

  const AsrPartialMessage({
    required this.speakerId,
    required this.text,
    required this.timestampMs,
  });

  factory AsrPartialMessage._fromRaw(List<dynamic> raw) {
    return AsrPartialMessage(
      speakerId: raw[1] as int,
      text: raw[2] as String,
      timestampMs: raw[3] as int,
    );
  }

  @override
  String toString() =>
      'AsrPartialMessage(speaker=$speakerId, text="$text", ts=$timestampMs)';
}

/// [2, speaker_id, text, timestamp_ms, segment_id] — Finalized ASR text.
final class AsrConfirmedMessage extends EchoMessage {
  final int speakerId;
  final String text;
  final int timestampMs;
  final int segmentId;

  const AsrConfirmedMessage({
    required this.speakerId,
    required this.text,
    required this.timestampMs,
    required this.segmentId,
  });

  factory AsrConfirmedMessage._fromRaw(List<dynamic> raw) {
    return AsrConfirmedMessage(
      speakerId: raw[1] as int,
      text: raw[2] as String,
      timestampMs: raw[3] as int,
      segmentId: raw[4] as int,
    );
  }

  @override
  String toString() =>
      'AsrConfirmedMessage(speaker=$speakerId, text="$text", '
      'ts=$timestampMs, seg=$segmentId)';
}

/// [3, speaker_id, token, segment_id] — Streaming translation token.
final class TranslationStreamMessage extends EchoMessage {
  final int speakerId;
  final String token;
  final int segmentId;

  const TranslationStreamMessage({
    required this.speakerId,
    required this.token,
    required this.segmentId,
  });

  factory TranslationStreamMessage._fromRaw(List<dynamic> raw) {
    return TranslationStreamMessage(
      speakerId: raw[1] as int,
      token: raw[2] as String,
      segmentId: raw[3] as int,
    );
  }

  @override
  String toString() =>
      'TranslationStreamMessage(speaker=$speakerId, token="$token", '
      'seg=$segmentId)';
}

/// [4, speaker_id, full_text, segment_id] — Translation segment complete.
final class TranslationDoneMessage extends EchoMessage {
  final int speakerId;
  final String fullText;
  final int segmentId;

  const TranslationDoneMessage({
    required this.speakerId,
    required this.fullText,
    required this.segmentId,
  });

  factory TranslationDoneMessage._fromRaw(List<dynamic> raw) {
    return TranslationDoneMessage(
      speakerId: raw[1] as int,
      fullText: raw[2] as String,
      segmentId: raw[3] as int,
    );
  }

  @override
  String toString() =>
      'TranslationDoneMessage(speaker=$speakerId, text="$fullText", '
      'seg=$segmentId)';
}

/// [5, speaker_id, segment_id] — TTS synthesis began for a segment.
final class TtsStartedMessage extends EchoMessage {
  final int speakerId;
  final int segmentId;

  const TtsStartedMessage({
    required this.speakerId,
    required this.segmentId,
  });

  factory TtsStartedMessage._fromRaw(List<dynamic> raw) {
    return TtsStartedMessage(
      speakerId: raw[1] as int,
      segmentId: raw[2] as int,
    );
  }

  @override
  String toString() =>
      'TtsStartedMessage(speaker=$speakerId, seg=$segmentId)';
}

/// [6, speaker_id, segment_id] — TTS synthesis finished for a segment.
final class TtsCompleteMessage extends EchoMessage {
  final int speakerId;
  final int segmentId;

  const TtsCompleteMessage({
    required this.speakerId,
    required this.segmentId,
  });

  factory TtsCompleteMessage._fromRaw(List<dynamic> raw) {
    return TtsCompleteMessage(
      speakerId: raw[1] as int,
      segmentId: raw[2] as int,
    );
  }

  @override
  String toString() =>
      'TtsCompleteMessage(speaker=$speakerId, seg=$segmentId)';
}

/// [10, error_code, model_name, detail] — Error notification.
final class ErrorMessage extends EchoMessage {
  final int errorCode;
  final String modelName;
  final String detail;

  const ErrorMessage({
    required this.errorCode,
    required this.modelName,
    required this.detail,
  });

  factory ErrorMessage._fromRaw(List<dynamic> raw) {
    return ErrorMessage(
      errorCode: raw[1] as int,
      modelName: raw[2] as String,
      detail: raw[3] as String,
    );
  }

  @override
  String toString() =>
      'ErrorMessage(code=$errorCode, model="$modelName", detail="$detail")';
}

/// [11, thermal_mode, temperature_c] — Thermal state change.
///
/// thermal_mode: 0=Normal, 1=Throttle, 2=Critical
final class ThermalStateMessage extends EchoMessage {
  final int thermalMode;
  final double temperatureC;

  const ThermalStateMessage({
    required this.thermalMode,
    required this.temperatureC,
  });

  factory ThermalStateMessage._fromRaw(List<dynamic> raw) {
    return ThermalStateMessage(
      thermalMode: raw[1] as int,
      temperatureC: (raw[2] as num).toDouble(),
    );
  }

  /// Human-readable thermal mode name.
  String get modeName => switch (thermalMode) {
        0 => 'Normal',
        1 => 'Throttle',
        2 => 'Critical',
        _ => 'Unknown($thermalMode)',
      };

  @override
  String toString() =>
      'ThermalStateMessage(mode=$modeName, temp=${temperatureC.toStringAsFixed(1)}°C)';
}

/// [12, current_bytes, limit_bytes, level] — Memory pressure event.
///
/// level: 1=85% warning, 2=95% critical
final class MemoryWarningMessage extends EchoMessage {
  final int currentBytes;
  final int limitBytes;
  final int level;

  const MemoryWarningMessage({
    required this.currentBytes,
    required this.limitBytes,
    required this.level,
  });

  factory MemoryWarningMessage._fromRaw(List<dynamic> raw) {
    return MemoryWarningMessage(
      currentBytes: raw[1] as int,
      limitBytes: raw[2] as int,
      level: raw[3] as int,
    );
  }

  /// Memory usage as a percentage.
  double get usagePercent =>
      limitBytes > 0 ? (currentBytes / limitBytes) * 100.0 : 0.0;

  @override
  String toString() =>
      'MemoryWarningMessage(level=$level, usage=${usagePercent.toStringAsFixed(1)}%)';
}

/// [13, stage, actual_ms, budget_ms] — SLA/latency violation.
final class LatencyWarningMessage extends EchoMessage {
  final String stage;
  final int actualMs;
  final int budgetMs;

  const LatencyWarningMessage({
    required this.stage,
    required this.actualMs,
    required this.budgetMs,
  });

  factory LatencyWarningMessage._fromRaw(List<dynamic> raw) {
    return LatencyWarningMessage(
      stage: raw[1] as String,
      actualMs: raw[2] as int,
      budgetMs: raw[3] as int,
    );
  }

  @override
  String toString() =>
      'LatencyWarningMessage(stage="$stage", actual=${actualMs}ms, '
      'budget=${budgetMs}ms)';
}

/// [14, dropped_samples, timestamp_ms] — Audio sample drop detected.
final class SampleDropMessage extends EchoMessage {
  final int droppedSamples;
  final int timestampMs;

  const SampleDropMessage({
    required this.droppedSamples,
    required this.timestampMs,
  });

  factory SampleDropMessage._fromRaw(List<dynamic> raw) {
    return SampleDropMessage(
      droppedSamples: raw[1] as int,
      timestampMs: raw[2] as int,
    );
  }

  @override
  String toString() =>
      'SampleDropMessage(dropped=$droppedSamples, ts=$timestampMs)';
}
