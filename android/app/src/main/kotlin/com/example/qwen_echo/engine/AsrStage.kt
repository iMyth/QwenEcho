package com.example.qwen_echo.engine

import android.util.Log
import com.k2fsa.sherpa.onnx.OfflineRecognizer
import com.k2fsa.sherpa.onnx.OfflineRecognizerConfig
import com.k2fsa.sherpa.onnx.OfflineModelConfig
import com.k2fsa.sherpa.onnx.OfflineSenseVoiceModelConfig
import com.k2fsa.sherpa.onnx.FeatureConfig
import java.io.File

/// ASR stage using sherpa-onnx offline recognizer with SenseVoice-Small.
///
/// Takes locked audio segments from the VAD, transcribes them with a
/// sherpa-onnx offline recognizer, and posts the result through MessageStream.
///
/// SenseVoice-Small is an offline (non-streaming) model that processes
/// the entire audio segment at once. It supports auto language detection
/// across zh, en, ja, yue, ko.
class AsrStage(
    private val messages: MessageStream,
) {
    private var recognizer: OfflineRecognizer? = null
    private var language: String = "auto"

    companion object {
        private const val TAG = "AsrStage"
    }

    /// Load the SenseVoice-Small model package at [path].
    /// The package must contain `model.int8.onnx` and `tokens.txt`.
    fun loadModel(path: String) {
        val modelFile = File(path, "model.int8.onnx")
        val tokensFile = File(path, "tokens.txt")

        if (!modelFile.exists()) {
            throw IllegalArgumentException("ASR package is missing model.int8.onnx at: ${modelFile.absolutePath}")
        }
        if (!tokensFile.exists()) {
            throw IllegalArgumentException("ASR package is missing tokens.txt at: ${tokensFile.absolutePath}")
        }

        val senseVoiceConfig = OfflineSenseVoiceModelConfig(
            model = modelFile.absolutePath,
            language = language,
            useInverseTextNormalization = true,
        )

        val modelConfig = OfflineModelConfig(
            senseVoice = senseVoiceConfig,
            numThreads = 2,
            tokens = tokensFile.absolutePath,
        )

        val featConfig = FeatureConfig(
            sampleRate = 16000,
            featureDim = 80,
        )

        val config = OfflineRecognizerConfig(
            featConfig = featConfig,
            modelConfig = modelConfig,
            decodingMethod = "greedy_search",
        )

        recognizer = OfflineRecognizer(config = config)
        Log.d(TAG, "SenseVoice model loaded from: $path")
    }

    /// Set the source language for recognition.
    /// SenseVoice supports: "auto", "zh", "en", "ja", "yue", "ko".
    /// "auto" enables automatic language detection.
    fun setLanguage(lang: String) {
        language = lang
        Log.d(TAG, "Set language: $lang")
    }

    /// Recognize speech from a locked audio segment.
    /// Returns the transcribed text, or empty string on failure.
    fun recognize(segment: LockedSegment): String {
        val rec = recognizer
        if (rec == null) {
            Log.e(TAG, "Recognizer not initialized")
            return ""
        }

        // Normalize Int16 samples to Float [-1, 1]
        val samples = FloatArray(segment.audioData.size) { i ->
            segment.audioData[i].toFloat() / Short.MAX_VALUE.toFloat()
        }
        if (samples.isEmpty()) {
            Log.w(TAG, "Empty audio segment")
            return ""
        }

        // Offline recognizer processes the entire audio at once
        val stream = rec.createStream()
        stream.acceptWaveform(samples, 16000)
        rec.decode(stream)
        val result = rec.getResult(stream)
        val text = result.text.trim()

        // Post result for UI consumption
        if (text.isNotEmpty()) {
            val detectedLang = if (result.lang.isNotEmpty()) result.lang else language
            Log.d(TAG, "Recognized ($detectedLang): $text")
            messages.post(EchoMessage.asrPartial(
                speakerId = segment.speakerId,
                text = text,
                segmentId = segment.segmentId,
            ))
        }

        return text
    }

    /// Release recognizer resources.
    fun release() {
        recognizer?.release()
        recognizer = null
    }
}
