import Foundation
import os
import MLXLLM
import MLXLMCommon

/// LLM translation stage using MLX for on-device inference.
///
/// Loads a Qwen3 model and performs streaming translation generation.
/// Maintains a sliding context window of recent translations.
final class LlmStage {

    private let messages: MessageStream
    private var modelContainer: ModelContainer?

    // Sliding context window for few-shot translation
    private var translationHistory: [(source: String, target: String)] = []
    private let maxHistoryEntries = 3

    // Language pair
    private var srcLang: String = "zh"
    private var tgtLang: String = "en"

    init(messages: MessageStream) {
        self.messages = messages
    }

    /// Load the LLM model from a directory path.
    /// MLX models are directories containing config.json, model.safetensors, etc.
    func loadModel(path: String) async throws {
        os_log("[LLM] Loading model from %{public}@", path)

        let dirURL = URL(fileURLWithPath: path)
        let config = ModelConfiguration(directory: dirURL)
        modelContainer = try await loadModelContainer(configuration: config)

        os_log("[LLM] Model loaded successfully")
    }

    /// Set the translation language pair.
    func setLanguagePair(src: String, tgt: String) {
        self.srcLang = src
        self.tgtLang = tgt
    }

    /// Translate the given text and stream tokens via MessageStream.
    /// Uses ChatSession for a clean, high-level API with streaming support.
    func translate(text: String, speakerId: Int, segmentId: Int) async {
        guard let modelContainer = modelContainer else {
            os_log("[LLM] Model not loaded — skipping translation")
            return
        }

        let prompt = buildTranslationPrompt(sourceText: text)
        os_log("[LLM] Translating: %{public}@", text)

        let params = GenerateParameters(maxTokens: 256, temperature: 0.3)
        let srcName = languageName(srcLang)
        let tgtName = languageName(tgtLang)
        let instructions = "You are a professional translator. Translate \(srcName) to \(tgtName). Provide only the translation."

        // Create a fresh ChatSession per translation for isolation
        let session = ChatSession(
            modelContainer,
            instructions: instructions,
            generateParameters: params
        )

        var fullTranslation = ""

        do {
            let stream = session.streamResponse(to: prompt)
            for try await chunk in stream {
                fullTranslation += chunk
                messages.post(.translationStream(
                    speakerId: speakerId,
                    token: chunk,
                    segmentId: segmentId
                ))
            }

            // Record translation in history for context window
            translationHistory.append((source: text, target: fullTranslation))
            if translationHistory.count > maxHistoryEntries {
                translationHistory.removeFirst()
            }

            messages.post(.translationDone(
                speakerId: speakerId,
                text: fullTranslation,
                segmentId: segmentId
            ))

            os_log("[LLM] Translation complete: %{public}@", fullTranslation)

        } catch {
            os_log("[LLM] Translation failed: %{public}@", error.localizedDescription)
            messages.post(.error(code: -10, detail: "Translation failed: \(error.localizedDescription)"))
        }
    }

    /// Build a translation prompt with few-shot context.
    private func buildTranslationPrompt(sourceText: String) -> String {
        let srcName = languageName(srcLang)
        let tgtName = languageName(tgtLang)

        var prompt = "Translate the following \(srcName) text to \(tgtName). Provide only the translation.\n\n"

        // Add few-shot examples from translation history
        for entry in translationHistory {
            prompt += "\(srcName): \(entry.source)\n"
            prompt += "\(tgtName): \(entry.target)\n\n"
        }

        prompt += "\(srcName): \(sourceText)\n"
        prompt += "\(tgtName): "

        return prompt
    }

    /// Map ISO 639-1 code to full language name.
    private func languageName(_ code: String) -> String {
        switch code {
        case "zh": return "Chinese"
        case "en": return "English"
        case "ja": return "Japanese"
        case "ko": return "Korean"
        case "fr": return "French"
        case "de": return "German"
        case "es": return "Spanish"
        case "ru": return "Russian"
        case "ar": return "Arabic"
        case "pt": return "Portuguese"
        case "it": return "Italian"
        case "th": return "Thai"
        case "vi": return "Vietnamese"
        default: return code.uppercased()
        }
    }
}
