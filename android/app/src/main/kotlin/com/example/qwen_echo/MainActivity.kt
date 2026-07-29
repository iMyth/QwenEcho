package com.example.qwen_echo

import com.example.qwen_echo.engine.EnginePlugin
import com.example.qwen_echo.engine.TtsPlugin
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.embedding.android.FlutterActivity

class MainActivity : FlutterActivity() {
    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        // Register our custom native engine plugins
        flutterEngine.plugins.add(EnginePlugin())
        flutterEngine.plugins.add(TtsPlugin())
    }
}
