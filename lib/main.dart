import 'package:flutter/material.dart';

import 'src/ui/home_screen.dart';

void main() {
  runApp(const QwenEchoApp());
}

/// Root application widget for QwenEcho simultaneous interpretation.
class QwenEchoApp extends StatelessWidget {
  const QwenEchoApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'QwenEcho',
      theme: ThemeData.dark().copyWith(
        scaffoldBackgroundColor: Colors.black,
        colorScheme: const ColorScheme.dark(
          primary: Color(0xFF00E676),
          secondary: Color(0xFF00E676),
        ),
      ),
      home: const HomeScreen(),
      debugShowCheckedModeBanner: false,
    );
  }
}
