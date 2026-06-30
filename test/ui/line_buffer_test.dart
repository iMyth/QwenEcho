import 'package:flutter_test/flutter_test.dart';
import 'package:qwen_echo/src/ui/line_buffer.dart';

void main() {
  late LineBuffer buffer;

  setUp(() {
    buffer = LineBuffer();
  });

  tearDown(() {
    buffer.dispose();
  });

  group('LineBuffer - partial lines', () {
    test('addPartialLine adds a gray partial line', () {
      buffer.addPartialLine('Hello');

      expect(buffer.lineCount, 1);
      expect(buffer.lines[0].text, 'Hello');
      expect(buffer.lines[0].state, LineState.partial);
    });

    test('addPartialLine replaces existing partial line', () {
      buffer.addPartialLine('Hel');
      buffer.addPartialLine('Hello wor');
      buffer.addPartialLine('Hello world');

      expect(buffer.lineCount, 1);
      expect(buffer.lines[0].text, 'Hello world');
      expect(buffer.lines[0].state, LineState.partial);
    });

    test('addPartialLine notifies listeners', () {
      int notifyCount = 0;
      buffer.addListener(() => notifyCount++);

      buffer.addPartialLine('test');
      expect(notifyCount, 1);

      buffer.addPartialLine('test updated');
      expect(notifyCount, 2);
    });
  });

  group('LineBuffer - confirmed lines', () {
    test('confirmLine replaces partial with confirmed', () {
      buffer.addPartialLine('Hello worl');
      buffer.confirmLine('Hello world.');

      expect(buffer.lineCount, 1);
      expect(buffer.lines[0].text, 'Hello world.');
      expect(buffer.lines[0].state, LineState.confirmed);
    });

    test('confirmLine appends if no partial exists', () {
      buffer.confirmLine('Direct confirmed text.');

      expect(buffer.lineCount, 1);
      expect(buffer.lines[0].text, 'Direct confirmed text.');
      expect(buffer.lines[0].state, LineState.confirmed);
    });

    test('after confirm, next addPartialLine creates new entry', () {
      buffer.addPartialLine('First');
      buffer.confirmLine('First sentence.');
      buffer.addPartialLine('Second');

      expect(buffer.lineCount, 2);
      expect(buffer.lines[0].state, LineState.confirmed);
      expect(buffer.lines[1].state, LineState.partial);
      expect(buffer.lines[1].text, 'Second');
    });
  });

  group('LineBuffer - translation lines', () {
    test('appendTranslationToken creates new translation line', () {
      buffer.appendTranslationToken('你');

      expect(buffer.lineCount, 1);
      expect(buffer.lines[0].text, '你');
      expect(buffer.lines[0].state, LineState.translation);
    });

    test('appendTranslationToken appends to existing translation', () {
      buffer.appendTranslationToken('你');
      buffer.appendTranslationToken('好');
      buffer.appendTranslationToken('世');
      buffer.appendTranslationToken('界');

      expect(buffer.lineCount, 1);
      expect(buffer.lines[0].text, '你好世界');
      expect(buffer.lines[0].state, LineState.translation);
    });

    test('completeTranslation finalizes translation line', () {
      buffer.appendTranslationToken('你');
      buffer.appendTranslationToken('好');
      buffer.completeTranslation('你好世界');

      expect(buffer.lineCount, 1);
      expect(buffer.lines[0].text, '你好世界');
      expect(buffer.lines[0].state, LineState.translation);
    });

    test('after completeTranslation, next token starts new line', () {
      buffer.appendTranslationToken('Hello');
      buffer.completeTranslation('Hello World');
      buffer.appendTranslationToken('New');

      expect(buffer.lineCount, 2);
      expect(buffer.lines[0].text, 'Hello World');
      expect(buffer.lines[1].text, 'New');
    });

    test('completeTranslation without prior tokens adds line', () {
      buffer.completeTranslation('Complete translation.');

      expect(buffer.lineCount, 1);
      expect(buffer.lines[0].text, 'Complete translation.');
      expect(buffer.lines[0].state, LineState.translation);
    });
  });

  group('LineBuffer - max lines enforcement', () {
    test('enforces 50-line maximum', () {
      for (int i = 0; i < 60; i++) {
        buffer.confirmLine('Line $i');
      }

      expect(buffer.lineCount, 50);
    });

    test('discards oldest lines when exceeding limit', () {
      for (int i = 0; i < 55; i++) {
        buffer.confirmLine('Line $i');
      }

      expect(buffer.lineCount, 50);
      // Oldest 5 lines (0-4) discarded, first remaining is Line 5
      expect(buffer.lines[0].text, 'Line 5');
      expect(buffer.lines[49].text, 'Line 54');
    });

    test('maxLines constant is 50', () {
      expect(LineBuffer.maxLines, 50);
    });
  });

  group('LineBuffer - mixed operations', () {
    test('interleaved partial, confirmed, and translation', () {
      buffer.addPartialLine('Hello');
      buffer.confirmLine('Hello world.');
      buffer.appendTranslationToken('你好');
      buffer.appendTranslationToken('世界');
      buffer.completeTranslation('你好世界');
      buffer.addPartialLine('Next');

      expect(buffer.lineCount, 3);
      expect(buffer.lines[0].state, LineState.confirmed);
      expect(buffer.lines[1].state, LineState.translation);
      expect(buffer.lines[2].state, LineState.partial);
    });

    test('clear resets all state', () {
      buffer.addPartialLine('partial');
      buffer.appendTranslationToken('token');
      buffer.confirmLine('confirmed');

      buffer.clear();

      expect(buffer.lineCount, 0);
      expect(buffer.lines, isEmpty);
    });

    test('after clear, new operations work correctly', () {
      buffer.confirmLine('Old line');
      buffer.clear();

      buffer.addPartialLine('Fresh start');
      expect(buffer.lineCount, 1);
      expect(buffer.lines[0].text, 'Fresh start');
      expect(buffer.lines[0].state, LineState.partial);
    });
  });

  group('DisplayLine', () {
    test('equality works correctly', () {
      const a = DisplayLine(text: 'hello', state: LineState.partial);
      const b = DisplayLine(text: 'hello', state: LineState.partial);
      const c = DisplayLine(text: 'hello', state: LineState.confirmed);

      expect(a, equals(b));
      expect(a, isNot(equals(c)));
    });

    test('copyWith creates modified copy', () {
      const line = DisplayLine(text: 'hello', state: LineState.partial);
      final updated = line.copyWith(text: 'hello world');

      expect(updated.text, 'hello world');
      expect(updated.state, LineState.partial);
    });
  });
}
