/// Typed message hierarchy for QwenEcho engine events.
///
/// Messages are received via EventChannel from the Swift native engine.
/// Each message is a Map with a "type" field that determines its structure.
library;

/// Base class for all engine messages.
sealed class EchoMessage {
  const EchoMessage();

  /// Parse a raw Map from EventChannel into a typed [EchoMessage].
  static EchoMessage? fromMap(Map<dynamic, dynamic> map) {
    final type = map['type'] as int?;
    if (type == null) return null;

    return switch (type) {
      0 => AsrPartialMessage(
          speakerId: map['speakerId'] as int? ?? 0,
          text: map['text'] as String? ?? '',
          segmentId: map['segmentId'] as int? ?? 0,
        ),
      1 => AsrConfirmedMessage(
          speakerId: map['speakerId'] as int? ?? 0,
          text: map['text'] as String? ?? '',
          segmentId: map['segmentId'] as int? ?? 0,
        ),
      2 => TranslationStreamMessage(
          speakerId: map['speakerId'] as int? ?? 0,
          token: map['text'] as String? ?? '',
          segmentId: map['segmentId'] as int? ?? 0,
        ),
      3 => TranslationDoneMessage(
          speakerId: map['speakerId'] as int? ?? 0,
          text: map['text'] as String? ?? '',
          segmentId: map['segmentId'] as int? ?? 0,
        ),
      4 => ErrorMessage(
          code: map['errorCode'] as int? ?? 0,
          detail: map['detail'] as String? ?? '',
        ),
      5 => ThermalStateMessage(
          mode: map['errorCode'] as int? ?? 0,
          detail: map['detail'] as String? ?? '',
        ),
      6 => LatencyWarningMessage(
          stage: map['detail'] as String? ?? '',
          actualMs: map['errorCode'] as int? ?? 0,
        ),
      7 => EngineReadyMessage(
          status: map['text'] as String? ?? '',
        ),
      _ => null,
    };
  }
}

/// Partial (unconfirmed) ASR text from the originating speaker.
class AsrPartialMessage extends EchoMessage {
  final int speakerId;
  final String text;
  final int segmentId;

  const AsrPartialMessage({
    required this.speakerId,
    required this.text,
    required this.segmentId,
  });

  @override
  String toString() =>
      'AsrPartialMessage(speaker=$speakerId, text="$text", seg=$segmentId)';
}

/// Confirmed ASR text from the originating speaker.
class AsrConfirmedMessage extends EchoMessage {
  final int speakerId;
  final String text;
  final int segmentId;

  const AsrConfirmedMessage({
    required this.speakerId,
    required this.text,
    required this.segmentId,
  });

  @override
  String toString() =>
      'AsrConfirmedMessage(speaker=$speakerId, text="$text", seg=$segmentId)';
}

/// Streaming translation token for the opposing speaker.
class TranslationStreamMessage extends EchoMessage {
  final int speakerId;
  final String token;
  final int segmentId;

  const TranslationStreamMessage({
    required this.speakerId,
    required this.token,
    required this.segmentId,
  });

  @override
  String toString() =>
      'TranslationStreamMessage(speaker=$speakerId, token="$token", seg=$segmentId)';
}

/// Translation completion marker.
class TranslationDoneMessage extends EchoMessage {
  final int speakerId;
  final String text;
  final int segmentId;

  const TranslationDoneMessage({
    required this.speakerId,
    required this.text,
    required this.segmentId,
  });

  @override
  String toString() =>
      'TranslationDoneMessage(speaker=$speakerId, text="$text", seg=$segmentId)';
}

/// Error message from the engine.
class ErrorMessage extends EchoMessage {
  final int code;
  final String detail;

  const ErrorMessage({required this.code, required this.detail});

  @override
  String toString() => 'ErrorMessage(code=$code, detail="$detail")';
}

/// Thermal state change notification.
class ThermalStateMessage extends EchoMessage {
  final int mode;
  final String detail;

  const ThermalStateMessage({required this.mode, required this.detail});

  @override
  String toString() => 'ThermalStateMessage(mode=$mode, detail="$detail")';
}

/// Latency SLA violation warning.
class LatencyWarningMessage extends EchoMessage {
  final String stage;
  final int actualMs;

  const LatencyWarningMessage({required this.stage, required this.actualMs});

  @override
  String toString() => 'LatencyWarningMessage(stage=$stage, ${actualMs}ms)';
}

/// Engine is ready (models loaded successfully).
class EngineReadyMessage extends EchoMessage {
  final String status;

  const EngineReadyMessage({required this.status});

  @override
  String toString() => 'EngineReadyMessage(status="$status")';
}
