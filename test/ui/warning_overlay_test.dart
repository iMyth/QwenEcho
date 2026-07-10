import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:qwen_echo/src/messages.dart';
import 'package:qwen_echo/src/ui/warning_overlay.dart';

void main() {
  group('WarningOverlay', () {
    testWidgets('shows nothing when no warnings received', (tester) async {
      final controller = StreamController<EchoMessage>.broadcast();
      addTearDown(controller.close);

      await tester.pumpWidget(MaterialApp(
        home: Scaffold(
          body: Stack(
            children: [
              WarningOverlay(messages: controller.stream),
            ],
          ),
        ),
      ));

      // No warning icons.
      expect(find.byIcon(Icons.warning_amber_rounded), findsNothing);
    });

    testWidgets('displays error message on ErrorMessage', (tester) async {
      final controller = StreamController<EchoMessage>.broadcast();
      addTearDown(controller.close);

      await tester.pumpWidget(MaterialApp(
        home: Scaffold(
          body: Stack(
            children: [
              WarningOverlay(messages: controller.stream),
            ],
          ),
        ),
      ));

      controller.add(const ErrorMessage(code: 1, detail: 'Model load failed'));
      await tester.pump();

      expect(find.textContaining('Model load failed'), findsOneWidget);
    });

    testWidgets('displays latency warning on LatencyWarningMessage',
        (tester) async {
      final controller = StreamController<EchoMessage>.broadcast();
      addTearDown(controller.close);

      await tester.pumpWidget(MaterialApp(
        home: Scaffold(
          body: Stack(
            children: [
              WarningOverlay(messages: controller.stream),
            ],
          ),
        ),
      ));

      controller.add(const LatencyWarningMessage(
        stage: 'ASR',
        actualMs: 350,
      ));
      await tester.pump();

      expect(find.textContaining('ASR latency'), findsOneWidget);
      expect(find.textContaining('350ms'), findsOneWidget);
    });

    testWidgets('limits visible warnings to maxVisible', (tester) async {
      final controller = StreamController<EchoMessage>.broadcast();
      addTearDown(controller.close);

      await tester.pumpWidget(MaterialApp(
        home: Scaffold(
          body: Stack(
            children: [
              WarningOverlay(
                messages: controller.stream,
                maxVisible: 2,
              ),
            ],
          ),
        ),
      ));

      // Send 3 warnings.
      controller
          .add(const LatencyWarningMessage(stage: 'ASR', actualMs: 300));
      controller
          .add(const LatencyWarningMessage(stage: 'LLM', actualMs: 600));
      controller
          .add(const LatencyWarningMessage(stage: 'TTS', actualMs: 200));
      await tester.pump();

      // Only 2 visible (max), oldest dropped.
      expect(find.byIcon(Icons.warning_amber_rounded), findsNWidgets(2));
      // ASR warning was trimmed (oldest).
      expect(find.textContaining('ASR'), findsNothing);
      expect(find.textContaining('LLM'), findsOneWidget);
      expect(find.textContaining('TTS'), findsOneWidget);
    });

    testWidgets('auto-dismisses warnings after duration', (tester) async {
      final controller = StreamController<EchoMessage>.broadcast();
      addTearDown(controller.close);

      var fakeNow = DateTime(2024, 1, 1);
      DateTime fakeClock() => fakeNow;

      await tester.pumpWidget(MaterialApp(
        home: Scaffold(
          body: Stack(
            children: [
              WarningOverlay(
                messages: controller.stream,
                displayDuration: const Duration(seconds: 2),
                clock: fakeClock,
              ),
            ],
          ),
        ),
      ));

      controller.add(
          const LatencyWarningMessage(stage: 'ASR', actualMs: 350));
      await tester.pump();

      expect(find.textContaining('ASR'), findsOneWidget);

      // Advance the fake clock past the display duration.
      fakeNow = fakeNow.add(const Duration(seconds: 3));
      // Pump to trigger the periodic cleanup timer.
      await tester.pump(const Duration(seconds: 1));

      expect(find.textContaining('ASR'), findsNothing);
    });

    testWidgets('ignores non-warning messages', (tester) async {
      final controller = StreamController<EchoMessage>.broadcast();
      addTearDown(controller.close);

      await tester.pumpWidget(MaterialApp(
        home: Scaffold(
          body: Stack(
            children: [
              WarningOverlay(messages: controller.stream),
            ],
          ),
        ),
      ));

      controller.add(const AsrPartialMessage(
          speakerId: 0, text: 'hello', segmentId: 0));
      controller
          .add(const ThermalStateMessage(mode: 1, detail: 'Throttle'));
      await tester.pump();

      // No warnings displayed.
      expect(find.byIcon(Icons.warning_amber_rounded), findsNothing);
    });
  });
}
