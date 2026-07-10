import Foundation
import Speech
import AVFoundation
import os

/// ASR stage using Apple's SFSpeechRecognizer for on-device speech recognition.
///
/// Takes locked audio segments from the VAD and transcribes them to text.
/// Streams partial results via MessageStream, returns final text for LLM translation.
final class AsrStage {

    private let messages: MessageStream
    private var speechRecognizer: SFSpeechRecognizer?
    private var locale: Locale = Locale(identifier: "zh-CN")

    init(messages: MessageStream) {
        self.messages = messages
    }

    /// Request speech recognition authorization from the user.
    /// Must be called before using recognize().
    static func requestAuthorization() async -> SFSpeechRecognizerAuthorizationStatus {
        await withCheckedContinuation { continuation in
            SFSpeechRecognizer.requestAuthorization { status in
                continuation.resume(returning: status)
            }
        }
    }

    /// Set the recognition language.
    func setLanguage(_ lang: String) {
        let localeId = mapLanguageToLocale(lang)
        locale = Locale(identifier: localeId)
        speechRecognizer = SFSpeechRecognizer(locale: locale)
        os_log("[ASR] Set language: %{public}@ (locale: %{public}@)", lang, localeId)
    }

    /// Recognize speech from a locked audio segment.
    /// Returns the transcribed text, or empty string on failure.
    func recognize(segment: LockedSegment) async -> String {
        guard let speechRecognizer = speechRecognizer else {
            os_log("[ASR] Speech recognizer not initialized")
            return ""
        }

        guard speechRecognizer.isAvailable else {
            os_log("[ASR] Speech recognizer not available")
            return ""
        }

        // Convert Int16 samples to AVAudioPCMBuffer
        guard let buffer = makePCMBuffer(from: segment.audioData) else {
            os_log("[ASR] Failed to create PCM buffer")
            return ""
        }

        let request = SFSpeechAudioBufferRecognitionRequest()
        request.shouldReportPartialResults = true
        request.append(buffer)
        request.endAudio()

        return await withCheckedContinuation { continuation in
            var resumed = false
            let lock = NSLock()

            let safeResume: (String) -> Void = { text in
                lock.lock()
                if !resumed {
                    resumed = true
                    lock.unlock()
                    continuation.resume(returning: text)
                } else {
                    lock.unlock()
                }
            }

            let task = speechRecognizer.recognitionTask(with: request) { [weak self] result, error in
                guard let self = self else {
                    safeResume("")
                    return
                }

                if let error = error {
                    os_log("[ASR] Recognition error: %{public}@", error.localizedDescription)
                    safeResume("")
                    return
                }

                if let result = result {
                    let text = result.bestTranscription.formattedString

                    if !result.isFinal {
                        self.messages.post(.asrPartial(
                            speakerId: segment.speakerId,
                            text: text,
                            segmentId: segment.segmentId
                        ))
                    }

                    if result.isFinal {
                        os_log("[ASR] Recognized: %{public}@", text)
                        safeResume(text)
                    }
                }
            }

            // Fallback: if recognition doesn't complete in 10s, return what we have
            DispatchQueue.global().asyncAfter(deadline: .now() + 10) {
                task.cancel()
                safeResume("")
            }
        }
    }

    // MARK: - Private

    /// Convert [Int16] samples to AVAudioPCMBuffer at 16kHz mono.
    private func makePCMBuffer(from samples: [Int16]) -> AVAudioPCMBuffer? {
        guard !samples.isEmpty else { return nil }

        guard let format = AVAudioFormat(
            commonFormat: .pcmFormatInt16,
            sampleRate: 16000,
            channels: 1,
            interleaved: false
        ) else {
            return nil
        }

        let frameCount = AVAudioFrameCount(samples.count)
        guard let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: frameCount) else {
            return nil
        }
        buffer.frameLength = frameCount

        guard let channelData = buffer.int16ChannelData else { return nil }
        let destPtr = channelData[0]

        for i in 0..<samples.count {
            destPtr[i] = samples[i]
        }

        return buffer
    }

    /// Map ISO 639-1 language code to locale identifier.
    private func mapLanguageToLocale(_ lang: String) -> String {
        switch lang {
        case "zh": return "zh-CN"
        case "en": return "en-US"
        case "ja": return "ja-JP"
        case "ko": return "ko-KR"
        case "fr": return "fr-FR"
        case "de": return "de-DE"
        case "es": return "es-ES"
        case "ru": return "ru-RU"
        case "ar": return "ar-SA"
        case "pt": return "pt-BR"
        case "it": return "it-IT"
        case "th": return "th-TH"
        case "vi": return "vi-VN"
        default: return "\(lang)-\(lang.uppercased())"
        }
    }
}
