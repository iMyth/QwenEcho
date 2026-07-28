package com.example.qwen_echo.engine

import android.app.Activity
import android.content.Context
import android.util.Log
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.embedding.engine.plugins.activity.ActivityAware
import io.flutter.embedding.engine.plugins.activity.ActivityPluginBinding
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import io.flutter.plugin.common.PluginRegistry

/// Flutter plugin bridge for the QwenEcho engine.
///
/// Exposes MethodChannel for lifecycle commands (init, start, stop)
/// and EventChannel for streaming messages (ASR, translation, errors).
///
/// Mirrors iOS EnginePlugin.swift exactly — same channel names, same method
/// names, same argument structure. The Dart side is platform-agnostic.
class EnginePlugin : FlutterPlugin, MethodChannel.MethodCallHandler,
    EventChannel.StreamHandler, ActivityAware {

    private var methodChannel: MethodChannel? = null
    private var eventChannel: EventChannel? = null
    private val messageStream = MessageStream()
    private var pipeline: PipelineController? = null
    private var activity: Activity? = null
    private var applicationContext: Context? = null

    // Keep plugin alive
    companion object {
        private const val TAG = "EnginePlugin"
        private var instance: EnginePlugin? = null
    }

    // MARK: - FlutterPlugin

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        instance = this
        applicationContext = binding.applicationContext

        methodChannel = MethodChannel(binding.binaryMessenger, "qwen_echo_engine").also {
            it.setMethodCallHandler(this)
        }

        eventChannel = EventChannel(binding.binaryMessenger, "qwen_echo_events").also {
            it.setStreamHandler(this)
        }

        Log.d(TAG, "Attached to engine")
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        methodChannel?.setMethodCallHandler(null)
        eventChannel?.setStreamHandler(null)
        methodChannel = null
        eventChannel = null
        pipeline = null
        instance = null
        Log.d(TAG, "Detached from engine")
    }

    // MARK: - ActivityAware

    override fun onAttachedToActivity(binding: ActivityPluginBinding) {
        activity = binding.activity
        binding.addRequestPermissionsResultListener { requestCode, _, grantResults ->
            if (requestCode == AudioCapture.REQUEST_RECORD_AUDIO) {
                val granted = grantResults.isNotEmpty() && grantResults.all { it == android.content.pm.PackageManager.PERMISSION_GRANTED }
                pipeline?.getAudioCapture()?.onPermissionResult(granted)
                return@addRequestPermissionsResultListener true
            }
            false
        }
    }

    override fun onDetachedFromActivityForConfigChanges() {
        activity = null
    }

    override fun onReattachedToActivityForConfigChanges(binding: ActivityPluginBinding) {
        activity = binding.activity
    }

    override fun onDetachedFromActivity() {
        activity = null
    }

    // MARK: - EventChannel.StreamHandler

    override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
        if (events != null) {
            messageStream.setSink(events)
        }
    }

    override fun onCancel(arguments: Any?) {
        messageStream.clearSink()
    }

    // MARK: - MethodChannel.MethodCallHandler

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        val ctx = applicationContext
        if (ctx == null) {
            result.error("no_context", "Application context not available", null)
            return
        }

        // Lazily create pipeline on first method call
        if (pipeline == null) {
            pipeline = PipelineController(ctx, messageStream)
        }

        when (call.method) {
            "initialize" -> handleInitialize(call, result)
            "start" -> handleStart(call, result)
            "stop" -> handleStop(result)
            "setLanguage" -> handleSetLanguage(call, result)
            "test_inject" -> handleTestInject(call, result)
            "getPlatformVersion" -> {
                result.success("Android ${android.os.Build.VERSION.RELEASE}")
            }
            else -> result.notImplemented()
        }
    }

    // MARK: - Method Handlers

    private fun handleInitialize(call: MethodCall, result: MethodChannel.Result) {
        val args = call.arguments as? Map<*, *>
        val asrPath = args?.get("asrPath") as? String
        val llmPath = args?.get("llmPath") as? String

        if (asrPath == null) {
            result.error("invalid_args", "Missing asrPath", null)
            return
        }

        Log.d(TAG, "initialize: asrPath=$asrPath, llmPath=$llmPath")
        // LLM path is handled by Dart-side llamadart; native side only needs ASR
        pipeline?.initialize(asrPath)
        result.success(mapOf("success" to true))
    }

    private fun handleStart(call: MethodCall, result: MethodChannel.Result) {
        val args = call.arguments as? Map<*, *>
        val srcLang = args?.get("srcLang") as? String ?: "zh"
        val tgtLang = args?.get("tgtLang") as? String ?: "en"

        // Request microphone permission if needed
        val act = activity
        if (act != null) {
            val permResult = pipeline?.getAudioCapture()?.requestPermission(act)
            if (permResult is AudioPermissionResult.Denied) {
                // Permission dialog shown — result will come via
                // onRequestPermissionsResult. For now, return a provisional
                // success; the actual start happens after permission is granted.
                Log.d(TAG, "Requesting microphone permission...")
            }
        }

        val error = pipeline?.start(srcLang, tgtLang)
        if (error != null) {
            result.error("start_failed", error, null)
        } else {
            result.success(mapOf("success" to true))
        }
    }

    private fun handleStop(result: MethodChannel.Result) {
        pipeline?.stop()
        result.success(mapOf("success" to true))
    }

    private fun handleSetLanguage(call: MethodCall, result: MethodChannel.Result) {
        val args = call.arguments as? Map<*, *>
        val srcLang = args?.get("srcLang") as? String
        val tgtLang = args?.get("tgtLang") as? String

        if (srcLang == null || tgtLang == null) {
            result.error("invalid_args", "Missing srcLang/tgtLang", null)
            return
        }

        pipeline?.setLanguage(srcLang, tgtLang)
        result.success(mapOf("success" to true))
    }

    private fun handleTestInject(call: MethodCall, result: MethodChannel.Result) {
        val args = call.arguments as? Map<*, *>
        val text = args?.get("text") as? String ?: ""
        val speakerId = (args?.get("speakerId") as? Int) ?: 0

        pipeline?.injectTestText(text, speakerId)
        result.success(mapOf("success" to true))
    }
}
