package com.myth.qwenecho.engine

import android.content.Context
import android.util.Log
import java.util.concurrent.Executors

/// Pipeline controller: orchestrates Audio Capture -> VAD -> ASR.
///
/// LLM translation runs in Dart via llamadart. The Android side only needs
/// to stream confirmed ASR text back to Flutter, where EchoEngine routes it
/// to the Dart LLM service.
///
/// Mirrors iOS PipelineController.swift.
class PipelineController(
    private val context: Context,
    private val messages: MessageStream,
) {
    private val audioCapture = AudioCapture()
    private val vad = VoiceActivityDetector()
    private val asrStage = AsrStage(messages)
    private val thermalMonitor = ThermalMonitor(context, messages)
    private val executor = Executors.newFixedThreadPool(2)

    // Model paths
    private var asrModelPath: String? = null

    // Pipeline state
    @Volatile
    private var isRunning = false

    // Language pair
    private var srcLang: String = "zh"
    private var tgtLang: String = "en"

    companion object {
        private const val TAG = "Pipeline"
    }

    /// Initialize with the ASR model package path.
    /// LLM is handled by the Dart-side llamadart service.
    fun initialize(asrPath: String) {
        asrModelPath = asrPath
        Log.d(TAG, "Initialized with ASR model: $asrPath")

        executor.execute {
            try {
                asrStage.loadModel(asrPath)
                messages.post(EchoMessage.engineReady("ASR model loaded"))
            } catch (e: Exception) {
                Log.e(TAG, "ASR model load failed", e)
                messages.post(EchoMessage.error(-3, "ASR model load failed: ${e.message}"))
            }
        }
    }

    /// Swap the source and target languages during an active session.
    fun setLanguage(srcLang: String, tgtLang: String) {
        this.srcLang = srcLang
        this.tgtLang = tgtLang
        asrStage.setLanguage(srcLang)
        Log.d(TAG, "Language swapped: $srcLang -> $tgtLang")
    }

    /// Start the interpretation pipeline.
    /// Returns null on success, or an error message on failure.
    fun start(srcLang: String, tgtLang: String): String? {
        if (isRunning) return null
        if (asrModelPath == null) {
            messages.post(EchoMessage.error(-1, "Engine not initialized"))
            return "Engine not initialized"
        }

        this.srcLang = srcLang
        this.tgtLang = tgtLang
        asrStage.setLanguage(srcLang)
        thermalMonitor.start()

        Log.d(TAG, "Starting: $srcLang -> $tgtLang")

        // Set up VAD callback
        vad.setSegmentCallback { segment ->
            processSegment(segment)
        }

        // Start audio capture
        val error = audioCapture.start { samples ->
            vad.feedAudio(samples)
        }

        if (error != null) {
            Log.e(TAG, "Audio capture failed: $error")
            messages.post(EchoMessage.error(-2, "Audio capture failed: $error"))
            audioCapture.stop()
            isRunning = false
            return error
        }

        isRunning = true
        return null
    }

    /// Stop the interpretation pipeline.
    /// Flushes any pending VAD buffer through ASR before teardown.
    fun stop() {
        if (!isRunning) return

        Log.d(TAG, "Stopping...")

        // 1. Stop pulling new audio from the mic
        audioCapture.stop()

        // 2. Flush any pending VAD buffer through ASR
        vad.flush()

        // 3. Tear down pipeline state
        isRunning = false
        vad.reset()
        thermalMonitor.stop()

        Log.d(TAG, "Stopped")
    }

    /// Inject a test ASR segment for simulator/emulator testing.
    fun injectTestText(text: String, speakerId: Int = 0) {
        if (!isRunning) {
            Log.w(TAG, "Cannot inject test text — pipeline not running")
            return
        }
        val segmentId = (1000..9999).random()
        Log.d(TAG, "Injecting test text: $text")
        messages.post(EchoMessage.asrPartial(
            speakerId = speakerId,
            text = text,
            segmentId = segmentId,
        ))
        messages.post(EchoMessage.asrConfirmed(
            speakerId = speakerId,
            text = text,
            segmentId = segmentId,
        ))
    }

    /// Get the AudioCapture instance for permission handling.
    fun getAudioCapture(): AudioCapture = audioCapture

    // MARK: - Private

    private fun processSegment(segment: LockedSegment) {
        if (!isRunning) return

        executor.execute {
            val asrText = asrStage.recognize(segment)
            if (asrText.isNotEmpty()) {
                messages.post(EchoMessage.asrConfirmed(
                    speakerId = segment.speakerId,
                    text = asrText,
                    segmentId = segment.segmentId,
                ))
            }
        }
    }
}
