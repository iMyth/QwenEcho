import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:qwen_echo/src/ui/speaker_half.dart';
import 'package:qwen_echo/src/ui/split_view.dart';

void main() {
  group('SplitView', () {
    testWidgets('renders two halves in a Column with 50/50 split',
        (tester) async {
      await tester.pumpWidget(const MaterialApp(home: SplitView()));

      // Find two SpeakerHalf widgets.
      final halfWidgets = find.byType(SpeakerHalf);
      expect(halfWidgets, findsNWidgets(2));

      // Both halves should be wrapped in Expanded.
      final expandedWidgets = find.byType(Expanded);
      expect(expandedWidgets, findsNWidgets(2));
    });

    testWidgets('top half is rotated 180 degrees', (tester) async {
      await tester.pumpWidget(const MaterialApp(home: SplitView()));

      // Find Transform widgets and check if any has a 180° rotation.
      final transforms = find.byType(Transform);
      bool foundRotation = false;
      for (int i = 0; i < transforms.evaluate().length; i++) {
        final transform =
            tester.widget<Transform>(transforms.at(i));
        final matrix = transform.transform;
        // A 180-degree rotation matrix has cos(pi) ≈ -1 at [0][0] and [1][1].
        if ((matrix.getColumn(0)[0] - (-1.0)).abs() < 0.001 &&
            (matrix.getColumn(1)[1] - (-1.0)).abs() < 0.001) {
          foundRotation = true;
          break;
        }
      }
      expect(foundRotation, isTrue,
          reason: 'Expected a Transform with 180° rotation for the top half');
    });

    testWidgets('locks orientation to portrait on init', (tester) async {
      // Track calls to SystemChrome.
      final calls = <MethodCall>[];
      tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
        SystemChannels.platform,
        (call) async {
          calls.add(call);
          return null;
        },
      );

      await tester.pumpWidget(const MaterialApp(home: SplitView()));

      // Verify setPreferredOrientations was called with portraitUp.
      final orientationCall = calls.firstWhere(
        (c) => c.method == 'SystemChrome.setPreferredOrientations',
        orElse: () => const MethodCall('notfound'),
      );
      expect(orientationCall.method, 'SystemChrome.setPreferredOrientations');
      expect(orientationCall.arguments, contains('DeviceOrientation.portraitUp'));
    });

    testWidgets('shows idle indicator when no text received', (tester) async {
      await tester.pumpWidget(const MaterialApp(home: SplitView()));

      // Both halves should show idle indicator text.
      expect(find.text(kIdleText), findsNWidgets(2));
    });

    testWidgets('each half displays ASR text independently', (tester) async {
      final key = GlobalKey<SplitViewState>();
      await tester.pumpWidget(MaterialApp(home: SplitView(key: key)));
      await tester.pumpAndSettle();

      // Add confirmed text to bottom speaker (id=0).
      key.currentState!.addAsrConfirmed(0, 'Hello from speaker 0');
      await tester.pumpAndSettle();

      expect(find.text('Hello from speaker 0'), findsOneWidget);
      // Top half should still show idle since nothing was sent to speaker 1.
      expect(find.text(kIdleText), findsOneWidget);
    });

    testWidgets('translation appears in opposing half', (tester) async {
      final key = GlobalKey<SplitViewState>();
      await tester.pumpWidget(MaterialApp(home: SplitView(key: key)));
      await tester.pumpAndSettle();

      // Speaker 0 produces translation → appears in speaker 1's (top) half.
      key.currentState!.addTranslation(0, 'Translated text');
      await tester.pumpAndSettle();

      expect(find.text('Translated text'), findsOneWidget);
    });

    testWidgets('both halves can receive messages simultaneously (full-duplex)',
        (tester) async {
      final key = GlobalKey<SplitViewState>();
      await tester.pumpWidget(MaterialApp(home: SplitView(key: key)));
      await tester.pumpAndSettle();

      // Both speakers produce text simultaneously.
      key.currentState!.addAsrConfirmed(0, 'Speaker 0 text');
      key.currentState!.addAsrConfirmed(1, 'Speaker 1 text');
      await tester.pumpAndSettle();

      expect(find.text('Speaker 0 text'), findsOneWidget);
      expect(find.text('Speaker 1 text'), findsOneWidget);
      // No idle indicators should remain.
      expect(find.text(kIdleText), findsNothing);
    });
  });

  group('SpeakerHalf line buffer', () {
    testWidgets('enforces max 50 lines limit', (tester) async {
      final key = GlobalKey<SpeakerHalfState>();
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: SpeakerHalf(key: key, speakerId: 0),
          ),
        ),
      );
      await tester.pumpAndSettle();

      // Add 60 lines.
      for (int i = 0; i < 60; i++) {
        key.currentState!.addLine('Line $i', kAsrConfirmedColor);
      }
      await tester.pumpAndSettle();

      // Verify the state only keeps 50 lines (oldest 10 discarded).
      expect(key.currentState!.lineCount, equals(50));
      expect(key.currentState!.isEmpty, isFalse);

      // The oldest lines (0–9) should not be in the buffer at all.
      expect(find.text('Line 0'), findsNothing);
      expect(find.text('Line 9'), findsNothing);

      // The most recent line should be visible (auto-scroll keeps it in view).
      expect(find.text('Line 59'), findsOneWidget);
    });

    testWidgets('shows idle indicator when empty', (tester) async {
      await tester.pumpWidget(
        const MaterialApp(
          home: Scaffold(
            body: SpeakerHalf(speakerId: 0),
          ),
        ),
      );

      expect(find.text(kIdleText), findsOneWidget);
    });

    testWidgets('updateLastLine replaces partial ASR text', (tester) async {
      final key = GlobalKey<SpeakerHalfState>();
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: SpeakerHalf(key: key, speakerId: 0),
          ),
        ),
      );
      await tester.pumpAndSettle();

      // Add a partial line.
      key.currentState!.updateLastLine('Hel', kAsrPartialColor);
      await tester.pumpAndSettle();
      expect(find.text('Hel'), findsOneWidget);

      // Update the partial line.
      key.currentState!.updateLastLine('Hello', kAsrPartialColor);
      await tester.pumpAndSettle();
      expect(find.text('Hel'), findsNothing);
      expect(find.text('Hello'), findsOneWidget);
    });
  });
}
