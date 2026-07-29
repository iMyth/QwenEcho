package com.example.qwen_echo.engine

import android.speech.tts.TextToSpeech
import android.speech.tts.UtteranceProgressListener
import android.util.Log
import java.util.Locale

/// Text-to-speech player using the Android system TextToSpeech engine.
///
/// Mirrors iOS TtsPlayer (AVSpeechSynthesizer). Uses QUEUE_FLUSH mode
/// so rapid consecutive translations don't queue up with growing delay.
class TtsPlayer(
    private val onInit: (Boolean) -> Unit = {},
) : TextToSpeech.OnInitListener {

    private var tts: TextToSpeech? = null
    private var isInitialized = false
    private var pendingInit = false

    companion object {
        private const val TAG = "TtsPlayer"
    }

    /// Initialize the TTS engine. Must be called before speak().
    /// The context must be an Activity or Application context.
    fun initialize(androidContext: android.content.Context) {
        if (tts != null) return
        pendingInit = true
        tts = TextToSpeech(androidContext, this)
    }

    /// Speak the given text in the specified language.
    /// Uses QUEUE_FLUSH to interrupt any in-progress speech (matches iOS behavior).
    fun speak(text: String, lang: String): Boolean {
        val engine = tts
        if (engine == null || !isInitialized) {
            Log.w(TAG, "TTS not initialized, cannot speak")
            return false
        }

        // Set language
        val locale = mapLanguageToLocale(lang)
        val result = engine.setLanguage(locale)
        if (result == TextToSpeech.LANG_MISSING_DATA || result == TextToSpeech.LANG_NOT_SUPPORTED) {
            Log.w(TAG, "Language not supported: $lang (locale=$locale)")
            return false
        }

        // Configure speech rate — slightly faster than default for prompt translation
        engine.setSpeechRate(1.1f)

        Log.d(TAG, "Speaking ($lang): $text")
        engine.speak(text, TextToSpeech.QUEUE_FLUSH, null, "qwen_echo_${System.currentTimeMillis()}")
        return true
    }

    /// Stop any in-progress speech.
    fun stop() {
        tts?.stop()
    }

    /// List available voices. Returns a list of {language, quality} maps.
    fun getVoices(): List<Map<String, Any>> {
        val engine = tts ?: return emptyList()
        return engine.voices.map { voice ->
            mapOf(
                "language" to (voice.locale?.toLanguageTag() ?: "unknown"),
                "quality" to (if (voice.isNetworkConnectionRequired) 0 else 1),
            )
        }
    }

    /// Release TTS resources.
    fun shutdown() {
        tts?.stop()
        tts?.shutdown()
        tts = null
        isInitialized = false
        pendingInit = false
    }

    // MARK: - OnInitListener

    override fun onInit(status: Int) {
        if (status == TextToSpeech.SUCCESS) {
            isInitialized = true
            Log.d(TAG, "TTS engine initialized successfully")
        } else {
            Log.e(TAG, "TTS engine initialization failed: status=$status")
            isInitialized = false
        }
        onInit(isInitialized)
    }

    /// Map our short language codes to Java Locale objects.
    private fun mapLanguageToLocale(lang: String): Locale {
        return when (lang) {
            "zh" -> Locale.CHINESE
            "en" -> Locale.ENGLISH
            "ja" -> Locale.JAPANESE
            "ko" -> Locale.KOREAN
            "fr" -> Locale.FRENCH
            "es" -> Locale("es")
            "de" -> Locale.GERMAN
            "ru" -> Locale("ru")
            "ar" -> Locale("ar")
            "pt" -> Locale("pt")
            else -> Locale(lang)
        }
    }
}
