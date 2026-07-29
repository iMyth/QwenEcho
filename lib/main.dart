import 'package:flutter/material.dart';

import 'src/model/model_repository.dart';
import 'src/ui/home_screen.dart';
import 'src/ui/model_download_screen.dart';

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
      home: const ModelCheckScreen(),
      debugShowCheckedModeBanner: false,
    );
  }
}

/// Screen that checks if models are downloaded and routes accordingly.
class ModelCheckScreen extends StatefulWidget {
  const ModelCheckScreen({super.key});

  @override
  State<ModelCheckScreen> createState() => _ModelCheckScreenState();
}

class _ModelCheckScreenState extends State<ModelCheckScreen> {
  final ModelRepository _repository = ModelRepository();
  bool _modelsReady = false;
  bool _checking = true;

  @override
  void initState() {
    super.initState();
    _checkModels();
  }

  Future<void> _checkModels() async {
    final ready = await _repository.isComplete;
    setState(() {
      _modelsReady = ready;
      _checking = false;
    });
  }

  @override
  Widget build(BuildContext context) {
    if (_checking) {
      return const Scaffold(
        body: Center(
          child: CircularProgressIndicator(),
        ),
      );
    }

    if (_modelsReady) {
      return const HomeScreen();
    }

    return ModelDownloadScreen(
      onComplete: () {
        setState(() {
          _modelsReady = true;
        });
      },
    );
  }
}
