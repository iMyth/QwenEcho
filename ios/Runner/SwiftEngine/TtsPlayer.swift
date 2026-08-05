import Foundation
import AVFAudio
import Flutter
import os

// MARK: - Notification Names for TTS Events

extension Notification.Name {
    static let ttsDidStart = Notification.Name("ttsDidStart")
    static let ttsDidStop = Notification.Name("ttsDidStop")
}

/// Text-to-speech player using the system AVSpeechSynthesizer.
///
/// QwenEcho currently uses the iOS system voices instead of a dedicated
/// Qwen3-TTS model. This keeps the binary size small, the power draw low,
/// and removes the need to ship a ~250MB TTS model. The Dart side invokes
/// `speak(text, lang:)` via a MethodChannel after translation completes.
///
/// Audio session integration: the player expects the audio session to be
/// configured as `.playAndRecord` (set by [AudioCapture] so that mic input
/// and speaker output coexist). We add `.mixWithOthers` behavior via
/// `.duckOthers` to avoid stepping on other audio.
@MainActor
final class TtsPlayer: NSObject, FlutterPlugin, AVSpeechSynthesizerDelegate {

    private let synthesizer = AVSpeechSynthesizer()
    private var completion: (() -> Void)?

    // Keep the plugin instance alive
    private static var instance: TtsPlayer?

    override init() {
        super.init()
        synthesizer.delegate = self
    }

    // MARK: - FlutterPlugin

    static func register(with registrar: FlutterPluginRegistrar) {
        let instance = TtsPlayer()
        Self.instance = instance

        let channel = FlutterMethodChannel(
            name: "qwen_echo_tts",
            binaryMessenger: registrar.messenger()
        )
        registrar.addMethodCallDelegate(instance, channel: channel)
    }

    func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        switch call.method {
        case "speak":
            handleSpeak(call, result: result)

        case "stop":
            synthesizer.stopSpeaking(at: .immediate)
            result(["success": true])

        case "getVoices":
            result(listVoices())

        default:
            result(FlutterMethodNotImplemented)
        }
    }

    // MARK: - Method Handlers

    private func handleSpeak(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        guard let args = call.arguments as? [String: Any],
              let text = args["text"] as? String,
              !text.isEmpty else {
            result(FlutterError(code: "invalid_args",
                                message: "Missing text to speak",
                                details: nil))
            return
        }
        let lang = args["lang"] as? String ?? "en"

        // Stop any in-flight utterance before starting a new one. This is
        // important for rapid-fire consecutive translations — otherwise the
        // synthesizer would queue them all up with a growing delay.
        if synthesizer.isSpeaking {
            synthesizer.stopSpeaking(at: .immediate)
        }

        guard let utterance = buildUtterance(text: text, lang: lang) else {
            os_log("[TtsPlayer] No voice available for lang=%{public}@ text=%{public}@",
                   lang, text)
            result(FlutterError(code: "no_voice",
                                message: "No TTS voice for language: \(lang)",
                                details: nil))
            return
        }

        // Configure for interpretation: slightly faster than default so the
        // user hears the translation promptly, but not so fast it's unintelligible.
        utterance.rate = 0.52
        utterance.pitchMultiplier = 1.0
        utterance.volume = 1.0
        utterance.preUtteranceDelay = 0.0
        utterance.postUtteranceDelay = 0.0

        os_log("[TtsPlayer] Speaking (%{public}@): %{public}@", lang, text)

        // We complete the MethodChannel call immediately so the Dart side
        // isn't blocked waiting for audio to finish. The actual playback
        // happens asynchronously; the delegate reports completion via logs
        // (and could push a TtsComplete event if we wire it up later).
        synthesizer.speak(utterance)
        result(["success": true])
    }

    // MARK: - Voice Selection

    /// Build a speech utterance with the best available voice for [lang].
    ///
    /// Prefers enhanced/premium voices when available (more natural sound),
    /// falls back to any voice matching the language prefix, and finally
    /// returns nil if nothing matches at all.
    private func buildUtterance(text: String, lang: String) -> AVSpeechUtterance? {
        let utterance = AVSpeechUtterance(string: text)

        // Map our short codes to BCP-47-ish prefixes AVFoundation uses.
        let prefix = bcp47Prefix(for: lang)

        let voices = AVSpeechSynthesisVoice.speechVoices()
        let matching = voices.filter { $0.language.hasPrefix(prefix) }

        guard !matching.isEmpty else {
            os_log("[TtsPlayer] No voice matches prefix=%{public}@", prefix)
            return nil
        }

        // Prefer enhanced quality voices; fall back to the first match.
        let preferred = matching.first(where: { $0.quality == .enhanced })
            ?? matching.first(where: { $0.quality == .premium })
            ?? matching.first!

        utterance.voice = preferred
        os_log("[TtsPlayer] Selected voice: %{public}@ (quality=%d)",
               preferred.language, preferred.quality.rawValue)
        return utterance
    }

    /// Map a QwenEcho language code to the BCP-47 prefix AVFoundation expects.
    private func bcp47Prefix(for lang: String) -> String {
        switch lang {
        case "zh": return "zh"
        case "en": return "en"
        case "ja": return "ja"
        case "ko": return "ko"
        case "fr": return "fr"
        case "es": return "es"
        case "de": return "de"
        case "ru": return "ru"
        case "ar": return "ar"
        case "pt": return "pt"
        default: return lang
        }
    }

    // MARK: - Diagnostics

    private func listVoices() -> [[String: Any]] {
        AVSpeechSynthesisVoice.speechVoices().map {
            ["language": $0.language, "quality": $0.quality.rawValue]
        }
    }

    // MARK: - AVSpeechSynthesizerDelegate

    func speechSynthesizer(_ synthesizer: AVSpeechSynthesizer,
                           didStart utterance: AVSpeechUtterance) {
        os_log("[TtsPlayer] didStart utterance")
        NotificationCenter.default.post(name: .ttsDidStart, object: nil)
    }

    func speechSynthesizer(_ synthesizer: AVSpeechSynthesizer,
                           didFinish utterance: AVSpeechUtterance) {
        os_log("[TtsPlayer] didFinish utterance")
        NotificationCenter.default.post(name: .ttsDidStop, object: nil)
        completion?()
        completion = nil
    }

    func speechSynthesizer(_ synthesizer: AVSpeechSynthesizer,
                           didCancel utterance: AVSpeechUtterance) {
        os_log("[TtsPlayer] didCancel utterance")
        NotificationCenter.default.post(name: .ttsDidStop, object: nil)
        completion?()
        completion = nil
    }
}
