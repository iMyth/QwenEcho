package com.example.qwen_echo.engine

import android.content.Context
import android.util.Log
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel

/// TTS plugin bridge for QwenEcho.
///
/// Exposes MethodChannel for text-to-speech commands (speak, stop, getVoices).
/// Uses the Android system TextToSpeech engine.
///
/// Mirrors iOS TtsPlayer.swift (FlutterPlugin part) — same channel name,
/// same method names, same argument structure.
class TtsPlugin : FlutterPlugin, MethodChannel.MethodCallHandler {

    private var methodChannel: MethodChannel? = null
    private val ttsPlayer = TtsPlayer()
    private var applicationContext: Context? = null

    companion object {
        private const val TAG = "TtsPlugin"
        private var instance: TtsPlugin? = null
    }

    // MARK: - FlutterPlugin

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        instance = this
        applicationContext = binding.applicationContext

        methodChannel = MethodChannel(binding.binaryMessenger, "qwen_echo_tts").also {
            it.setMethodCallHandler(this)
        }

        // Initialize TTS engine
        ttsPlayer.initialize(binding.applicationContext)
        Log.d(TAG, "Attached to engine")
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        methodChannel?.setMethodCallHandler(null)
        methodChannel = null
        ttsPlayer.shutdown()
        instance = null
        Log.d(TAG, "Detached from engine")
    }

    // MARK: - MethodChannel.MethodCallHandler

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        when (call.method) {
            "speak" -> handleSpeak(call, result)
            "stop" -> {
                ttsPlayer.stop()
                result.success(mapOf("success" to true))
            }
            "getVoices" -> {
                result.success(ttsPlayer.getVoices())
            }
            else -> result.notImplemented()
        }
    }

    private fun handleSpeak(call: MethodCall, result: MethodChannel.Result) {
        val args = call.arguments as? Map<*, *>
        val text = args?.get("text") as? String
        val lang = args?.get("lang") as? String ?: "en"

        if (text == null || text.isEmpty()) {
            result.error("invalid_args", "Missing text to speak", null)
            return
        }

        Log.d(TAG, "speak: text=$text, lang=$lang")
        val success = ttsPlayer.speak(text, lang)
        if (success) {
            result.success(mapOf("success" to true))
        } else {
            result.error("no_voice", "No TTS voice for language: $lang", null)
        }
    }
}
