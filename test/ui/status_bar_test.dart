import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:qwen_echo/src/messages.dart';
import 'package:qwen_echo/src/ui/status_bar.dart';

void main() {
  group('StatusBar', () {
    testWidgets('displays persistent OFFLINE badge', (tester) async {
      final controller = StreamController<EchoMessage>.broadcast();
      addTearDown(controller.close);

      await tester.pumpWidget(MaterialApp(
        home: Scaffold(
          body: StatusBar(messages: controller.stream),
        ),
      ));

      // OFFLINE badge is always visible.
      expect(find.text('OFFLINE'), findsOneWidget);
    });

    testWidgets('displays Normal thermal mode by default', (tester) async {
      final controller = StreamController<EchoMessage>.broadcast();
      addTearDown(controller.close);

      await tester.pumpWidget(MaterialApp(
        home: Scaffold(
          body: StatusBar(messages: controller.stream),
        ),
      ));

      expect(find.text('Normal'), findsOneWidget);
    });

    testWidgets('updates thermal indicator on ThermalStateMessage',
        (tester) async {
      final controller = StreamController<EchoMessage>.broadcast();
      addTearDown(controller.close);

      await tester.pumpWidget(MaterialApp(
        home: Scaffold(
          body: StatusBar(messages: controller.stream),
        ),
      ));

      // Default is Normal.
      expect(find.text('Normal'), findsOneWidget);

      // Send Throttle mode.
      controller.add(const ThermalStateMessage(thermalMode: 1, temperatureC: 44.0));
      await tester.pumpAndSettle();

      expect(find.text('Throttle'), findsOneWidget);
      expect(find.text('Normal'), findsNothing);

      // Send Critical mode.
      controller.add(const ThermalStateMessage(thermalMode: 2, temperatureC: 51.0));
      await tester.pumpAndSettle();

      expect(find.text('Critical'), findsOneWidget);
      expect(find.text('Throttle'), findsNothing);

      // Return to Normal.
      controller.add(const ThermalStateMessage(thermalMode: 0, temperatureC: 40.0));
      await tester.pumpAndSettle();

      expect(find.text('Normal'), findsOneWidget);
    });

    testWidgets('ignores non-thermal messages for indicator', (tester) async {
      final controller = StreamController<EchoMessage>.broadcast();
      addTearDown(controller.close);

      await tester.pumpWidget(MaterialApp(
        home: Scaffold(
          body: StatusBar(messages: controller.stream),
        ),
      ));

      // Send a non-thermal message.
      controller.add(const AsrPartialMessage(
          speakerId: 0, text: 'hello', timestampMs: 100));
      await tester.pump();

      // Still Normal.
      expect(find.text('Normal'), findsOneWidget);
    });
  });

  group('ThermalMode', () {
    test('fromCode maps integer codes correctly', () {
      expect(ThermalMode.fromCode(0), ThermalMode.normal);
      expect(ThermalMode.fromCode(1), ThermalMode.throttle);
      expect(ThermalMode.fromCode(2), ThermalMode.critical);
      expect(ThermalMode.fromCode(99), ThermalMode.normal); // fallback
    });

    test('labels are correct', () {
      expect(ThermalMode.normal.label, 'Normal');
      expect(ThermalMode.throttle.label, 'Throttle');
      expect(ThermalMode.critical.label, 'Critical');
    });
  });
}
