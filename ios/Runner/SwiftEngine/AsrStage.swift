import Foundation
import AVFoundation
import os

/// ASR stage using sherpa-onnx offline recognizer with SenseVoice-Small.
///
/// Takes locked audio segments from the VAD, transcribes them with a
/// sherpa-onnx offline recognizer, and posts the result through
/// [MessageStream].
///
/// SenseVoice-Small is an offline (non-streaming) model that processes
/// the entire audio segment at once. It supports auto language detection
/// across zh, en, ja, yue, ko.
final class AsrStage {

    private let messages: MessageStream
    private var recognizer: SherpaOnnxOfflineRecognizer?
    private var language: String = "auto"

    init(messages: MessageStream) {
        self.messages = messages
    }

    /// Load the SenseVoice-Small model package at [path].
    ///
    /// The package must contain `model.int8.onnx` and `tokens.txt`.
    func loadModel(path: String) async throws {
        let config = try buildRecognizerConfig(at: URL(fileURLWithPath: path))
        var mutableConfig = config
        let recognizer = withUnsafePointer(to: &mutableConfig) {
            SherpaOnnxOfflineRecognizer(config: $0)
        }
        self.recognizer = recognizer
        os_log("[ASR] SenseVoice model loaded from: %{public}@", path)
    }

    /// Set the source language for recognition.
    ///
    /// SenseVoice supports: "auto", "zh", "en", "ja", "yue", "ko".
    /// "auto" enables automatic language detection.
    func setLanguage(_ lang: String) {
        language = lang
        os_log("[ASR] Set language: %{public}@", lang)
    }

    /// Recognize speech from a locked audio segment.
    /// Returns the transcribed text, or empty string on failure.
    func recognize(segment: LockedSegment) async -> String {
        guard let recognizer = recognizer else {
            os_log("[ASR] Recognizer not initialized")
            return ""
        }

        let samples = normalize(samples: segment.audioData)
        guard !samples.isEmpty else {
            os_log("[ASR] Empty audio segment")
            return ""
        }

        // Offline recognizer processes the entire audio at once.
        let result = recognizer.decode(samples: samples, sampleRate: 16000)
        let text = result.text.trimmingCharacters(in: .whitespacesAndNewlines)

        // Post result for UI consumption.
        if !text.isEmpty {
            os_log("[ASR] Recognized (%{public}@): %{public}@",
                   result.lang.isEmpty ? language : result.lang, text)
            messages.post(.asrPartial(
                speakerId: segment.speakerId,
                text: text,
                segmentId: segment.segmentId
            ))
        }

        return text
    }

    // MARK: - Private

    private enum AsrError: LocalizedError {
        case missingTokens
        case missingModel

        var errorDescription: String? {
            switch self {
            case .missingTokens:
                return "ASR package is missing tokens.txt"
            case .missingModel:
                return "ASR package is missing model.int8.onnx"
            }
        }
    }

    /// Convert [Int16] samples to [Float] normalized to [-1, 1].
    private func normalize(samples: [Int16]) -> [Float] {
        return samples.map { Float($0) / Float(Int16.max) }
    }

    /// Build a sherpa-onnx offline recognizer config for SenseVoice-Small.
    private func buildRecognizerConfig(at dir: URL) throws -> SherpaOnnxOfflineRecognizerConfig {
        let fm = FileManager.default

        let tokensPath = dir.appendingPathComponent("tokens.txt").path
        guard fm.fileExists(atPath: tokensPath) else {
            throw AsrError.missingTokens
        }

        // sherpa-onnx SenseVoice releases use model.int8.onnx.
        let modelPath = dir.appendingPathComponent("model.int8.onnx").path
        guard fm.fileExists(atPath: modelPath) else {
            throw AsrError.missingModel
        }

        let senseVoice = sherpaOnnxOfflineSenseVoiceModelConfig(
            model: modelPath,
            language: language,
            useInverseTextNormalization: true
        )

        let modelConfig = sherpaOnnxOfflineModelConfig(
            tokens: tokensPath,
            numThreads: 2,
            senseVoice: senseVoice
        )

        let featConfig = sherpaOnnxFeatureConfig(sampleRate: 16000, featureDim: 80)

        return sherpaOnnxOfflineRecognizerConfig(
            featConfig: featConfig,
            modelConfig: modelConfig,
            decodingMethod: "greedy_search"
        )
    }
}
