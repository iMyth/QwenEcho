import 'package:flutter_test/flutter_test.dart';
import 'package:qwen_echo/src/messages.dart';

void main() {
  group('EchoMessage.fromRawList', () {
    test('parses AsrPartialMessage (type 1)', () {
      final raw = [1, 0, 'Hello', 12345];
      final msg = EchoMessage.fromRawList(raw);

      expect(msg, isA<AsrPartialMessage>());
      final asr = msg as AsrPartialMessage;
      expect(asr.speakerId, 0);
      expect(asr.text, 'Hello');
      expect(asr.timestampMs, 12345);
    });

    test('parses AsrConfirmedMessage (type 2)', () {
      final raw = [2, 1, 'Hello world.', 99999, 42];
      final msg = EchoMessage.fromRawList(raw);

      expect(msg, isA<AsrConfirmedMessage>());
      final asr = msg as AsrConfirmedMessage;
      expect(asr.speakerId, 1);
      expect(asr.text, 'Hello world.');
      expect(asr.timestampMs, 99999);
      expect(asr.segmentId, 42);
    });

    test('parses TranslationStreamMessage (type 3)', () {
      final raw = [3, 0, '你', 7];
      final msg = EchoMessage.fromRawList(raw);

      expect(msg, isA<TranslationStreamMessage>());
      final t = msg as TranslationStreamMessage;
      expect(t.speakerId, 0);
      expect(t.token, '你');
      expect(t.segmentId, 7);
    });

    test('parses TranslationDoneMessage (type 4)', () {
      final raw = [4, 1, '你好世界', 7];
      final msg = EchoMessage.fromRawList(raw);

      expect(msg, isA<TranslationDoneMessage>());
      final t = msg as TranslationDoneMessage;
      expect(t.speakerId, 1);
      expect(t.fullText, '你好世界');
      expect(t.segmentId, 7);
    });

    test('parses TtsStartedMessage (type 5)', () {
      final raw = [5, 0, 10];
      final msg = EchoMessage.fromRawList(raw);

      expect(msg, isA<TtsStartedMessage>());
      final t = msg as TtsStartedMessage;
      expect(t.speakerId, 0);
      expect(t.segmentId, 10);
    });

    test('parses TtsCompleteMessage (type 6)', () {
      final raw = [6, 1, 10];
      final msg = EchoMessage.fromRawList(raw);

      expect(msg, isA<TtsCompleteMessage>());
      final t = msg as TtsCompleteMessage;
      expect(t.speakerId, 1);
      expect(t.segmentId, 10);
    });

    test('parses ErrorMessage (type 10)', () {
      final raw = [10, -3, 'asr', 'File not found: /path/to/model.gguf'];
      final msg = EchoMessage.fromRawList(raw);

      expect(msg, isA<ErrorMessage>());
      final e = msg as ErrorMessage;
      expect(e.errorCode, -3);
      expect(e.modelName, 'asr');
      expect(e.detail, 'File not found: /path/to/model.gguf');
    });

    test('parses ThermalStateMessage (type 11)', () {
      final raw = [11, 1, 44.5];
      final msg = EchoMessage.fromRawList(raw);

      expect(msg, isA<ThermalStateMessage>());
      final t = msg as ThermalStateMessage;
      expect(t.thermalMode, 1);
      expect(t.temperatureC, 44.5);
      expect(t.modeName, 'Throttle');
    });

    test('parses MemoryWarningMessage (type 12)', () {
      final raw = [12, 2200000000, 2500000000, 1];
      final msg = EchoMessage.fromRawList(raw);

      expect(msg, isA<MemoryWarningMessage>());
      final m = msg as MemoryWarningMessage;
      expect(m.currentBytes, 2200000000);
      expect(m.limitBytes, 2500000000);
      expect(m.level, 1);
      expect(m.usagePercent, closeTo(88.0, 0.1));
    });

    test('parses LatencyWarningMessage (type 13)', () {
      final raw = [13, 'ASR', 350, 200];
      final msg = EchoMessage.fromRawList(raw);

      expect(msg, isA<LatencyWarningMessage>());
      final l = msg as LatencyWarningMessage;
      expect(l.stage, 'ASR');
      expect(l.actualMs, 350);
      expect(l.budgetMs, 200);
    });

    test('parses SampleDropMessage (type 14)', () {
      final raw = [14, 320, 55000];
      final msg = EchoMessage.fromRawList(raw);

      expect(msg, isA<SampleDropMessage>());
      final s = msg as SampleDropMessage;
      expect(s.droppedSamples, 320);
      expect(s.timestampMs, 55000);
    });

    test('returns null for empty list', () {
      expect(EchoMessage.fromRawList([]), isNull);
    });

    test('returns null for unknown type tag', () {
      expect(EchoMessage.fromRawList([99, 'unknown']), isNull);
    });
  });
}
