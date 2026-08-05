package com.myth.qwenecho.engine

import android.app.Activity
import android.content.Context
import android.util.Log
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.embedding.engine.plugins.activity.ActivityAware
import io.flutter.embedding.engine.plugins.activity.ActivityPluginBinding
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel

/// TTS plugin bridge for QwenEcho.
///
/// Implements ActivityAware to get Activity context for TTS initialization.
/// Android's TextToSpeech requires Activity context on some devices.
class TtsPlugin : FlutterPlugin, MethodChannel.MethodCallHandler, ActivityAware {

    private var methodChannel: MethodChannel? = null
    private val ttsPlayer = TtsPlayer()
    private var activity: Activity? = null

    companion object {
        private const val TAG = "TtsPlugin"
    }

    // MARK: - FlutterPlugin

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        methodChannel = MethodChannel(binding.binaryMessenger, "qwen_echo_tts").also {
            it.setMethodCallHandler(this)
        }
        Log.d(TAG, "Attached to engine")
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        methodChannel?.setMethodCallHandler(null)
        methodChannel = null
        ttsPlayer.shutdown()
        Log.d(TAG, "Detached from engine")
    }

    // MARK: - ActivityAware

    override fun onAttachedToActivity(binding: ActivityPluginBinding) {
        activity = binding.activity
        // Initialize TTS with Activity context
        ttsPlayer.initialize(binding.activity)
        Log.d(TAG, "Attached to activity — TTS initialized")
    }

    override fun onDetachedFromActivityForConfigChanges() {
        activity = null
    }

    override fun onReattachedToActivityForConfigChanges(binding: ActivityPluginBinding) {
        activity = binding.activity
        ttsPlayer.initialize(binding.activity)
    }

    override fun onDetachedFromActivity() {
        activity = null
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
