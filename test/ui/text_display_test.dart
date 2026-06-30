import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:qwen_echo/src/ui/line_buffer.dart';
import 'package:qwen_echo/src/ui/text_display.dart';

void main() {
  group('TextDisplay widget', () {
    late LineBuffer lineBuffer;

    setUp(() {
      lineBuffer = LineBuffer();
    });

    tearDown(() {
      lineBuffer.dispose();
    });

    Widget buildTestWidget({LineBuffer? buffer}) {
      return MaterialApp(
        home: Scaffold(
          body: SizedBox(
            height: 400,
            width: 300,
            child: TextDisplay(lineBuffer: buffer ?? lineBuffer),
          ),
        ),
      );
    }

    testWidgets('renders empty state with no lines', (tester) async {
      await tester.pumpWidget(buildTestWidget());

      // No text widgets from our buffer
      expect(find.byType(ListView), findsOneWidget);
    });

    testWidgets('renders partial text in gray', (tester) async {
      lineBuffer.addPartialLine('Hello');
      await tester.pumpWidget(buildTestWidget());

      final textWidget = tester.widget<Text>(find.text('Hello'));
      expect(textWidget.style?.color, TextDisplayColors.partial);
    });

    testWidgets('renders confirmed text in white', (tester) async {
      lineBuffer.confirmLine('Hello world.');
      await tester.pumpWidget(buildTestWidget());

      final textWidget = tester.widget<Text>(find.text('Hello world.'));
      expect(textWidget.style?.color, TextDisplayColors.confirmed);
    });

    testWidgets('renders translation text in green', (tester) async {
      lineBuffer.appendTranslationToken('你好');
      await tester.pumpWidget(buildTestWidget());

      final textWidget = tester.widget<Text>(find.text('你好'));
      expect(textWidget.style?.color, TextDisplayColors.translation);
    });

    testWidgets('updates when lineBuffer changes', (tester) async {
      await tester.pumpWidget(buildTestWidget());

      lineBuffer.addPartialLine('Typing...');
      await tester.pump();

      expect(find.text('Typing...'), findsOneWidget);

      lineBuffer.confirmLine('Typed sentence.');
      await tester.pump();

      expect(find.text('Typed sentence.'), findsOneWidget);
      expect(find.text('Typing...'), findsNothing);
    });

    testWidgets('renders multiple lines with correct colors', (tester) async {
      lineBuffer.confirmLine('Confirmed line.');
      lineBuffer.appendTranslationToken('Translation');
      lineBuffer.completeTranslation('Translation done.');
      lineBuffer.addPartialLine('Partial...');
      await tester.pumpWidget(buildTestWidget());

      final confirmed = tester.widget<Text>(find.text('Confirmed line.'));
      expect(confirmed.style?.color, TextDisplayColors.confirmed);

      final translation = tester.widget<Text>(find.text('Translation done.'));
      expect(translation.style?.color, TextDisplayColors.translation);

      final partial = tester.widget<Text>(find.text('Partial...'));
      expect(partial.style?.color, TextDisplayColors.partial);
    });

    testWidgets('color constants match spec values', (tester) async {
      expect(TextDisplayColors.partial, const Color(0xFF9E9E9E));
      expect(TextDisplayColors.confirmed, const Color(0xFFFFFFFF));
      expect(TextDisplayColors.translation, const Color(0xFF00E676));
    });
  });
}
