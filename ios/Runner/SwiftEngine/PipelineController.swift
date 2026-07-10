import Foundation
import os

/// Pipeline controller: orchestrates Audio Capture -> VAD -> ASR -> LLM.
///
/// Manages lifecycle of all pipeline components. Runs stages concurrently
/// using Swift async/await and TaskGroups.
final class PipelineController {

    private let messages: MessageStream
    private let audioCapture = AudioCapture()
    private let vad = VoiceActivityDetector()
    private let llmStage: LlmStage
    private let asrStage: AsrStage
    private let thermalMonitor: ThermalMonitor

    // Model paths
    private var llmModelPath: String?

    // Pipeline state
    private var isRunning = false
    private var pipelineTask: Task<Void, Never>?

    // Language pair
    private var srcLang: String = "zh"
    private var tgtLang: String = "en"

    init(messages: MessageStream) {
        self.messages = messages
        self.llmStage = LlmStage(messages: messages)
        self.asrStage = AsrStage(messages: messages)
        self.thermalMonitor = ThermalMonitor(messages: messages)
    }

    /// Initialize with LLM model directory path.
    /// ASR uses SFSpeechRecognizer (built-in, no model file needed).
    /// TTS is deferred to Phase 5.
    func initialize(llmPath: String) {
        self.llmModelPath = llmPath

        os_log("[Pipeline] Initialized with LLM model: %{public}@", llmPath)

        // Load LLM model asynchronously
        Task {
            do {
                try await llmStage.loadModel(path: llmPath)
                messages.post(.engineReady(status: "LLM model loaded"))
            } catch {
                os_log("[Pipeline] LLM model load failed: %{public}@", error.localizedDescription)
                messages.post(.error(code: -3, detail: "LLM model load failed: \(error.localizedDescription)"))
            }
        }
    }

    /// Start the interpretation pipeline.
    func start(srcLang: String, tgtLang: String) {
        guard !isRunning else { return }
        guard llmModelPath != nil else {
            messages.post(.error(code: -1, detail: "Engine not initialized"))
            return
        }

        self.srcLang = srcLang
        self.tgtLang = tgtLang
        self.isRunning = true

        llmStage.setLanguagePair(src: srcLang, tgt: tgtLang)
        asrStage.setLanguage(srcLang)
        thermalMonitor.start()

        os_log("[Pipeline] Starting: %{public}@ -> %{public}@", srcLang, tgtLang)

        // Set up VAD callback
        vad.setSegmentCallback { [weak self] segment in
            self?.processSegment(segment)
        }

        // Start audio capture
        do {
            try audioCapture.start { [weak self] samples in
                self?.vad.feedAudio(samples)
            }
        } catch {
            os_log("[Pipeline] Audio capture failed: %{public}@", error.localizedDescription)
            messages.post(.error(code: -2, detail: "Audio capture failed: \(error.localizedDescription)"))
            isRunning = false
        }
    }

    /// Stop the interpretation pipeline.
    func stop() {
        guard isRunning else { return }

        os_log("[Pipeline] Stopping...")

        isRunning = false
        audioCapture.stop()
        vad.reset()
        thermalMonitor.stop()

        pipelineTask?.cancel()
        pipelineTask = nil

        os_log("[Pipeline] Stopped")
    }

    // MARK: - Segment Processing

    /// Process a locked audio segment through ASR -> LLM.
    private func processSegment(_ segment: LockedSegment) {
        guard isRunning else { return }

        pipelineTask = Task.detached { [weak self] in
            guard let self = self else { return }

            // Phase 1: ASR (SFSpeechRecognizer)
            let asrText = await self.runASR(segment: segment)
            guard !asrText.isEmpty else { return }

            // Send confirmed ASR text
            self.messages.post(.asrConfirmed(
                speakerId: segment.speakerId,
                text: asrText,
                segmentId: segment.segmentId
            ))

            // Phase 2: LLM translation (MLX)
            await self.runLLM(segment: segment, inputText: asrText)
        }
    }

    // MARK: - ASR Stage (SFSpeechRecognizer)

    private func runASR(segment: LockedSegment) async -> String {
        await asrStage.recognize(segment: segment)
    }

    // MARK: - LLM Translation Stage (MLX)

    private func runLLM(segment: LockedSegment, inputText: String) async {
        await llmStage.translate(text: inputText, speakerId: segment.speakerId, segmentId: segment.segmentId)
    }
}
