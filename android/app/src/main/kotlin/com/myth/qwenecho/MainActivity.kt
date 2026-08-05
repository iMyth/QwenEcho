package com.myth.qwenecho

import com.myth.qwenecho.engine.EnginePlugin
import com.myth.qwenecho.engine.TtsPlugin
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
