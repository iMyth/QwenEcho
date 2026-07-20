import Foundation
import AVFAudio
import os

/// Pipeline controller: orchestrates Audio Capture -> VAD -> ASR.
///
/// LLM translation now runs in Dart via llamadart. The Swift side only needs
/// to stream confirmed ASR text back to Flutter, where [EchoEngine] routes it
/// to the Dart LLM service.
final class PipelineController {

    private let messages: MessageStream
    private let audioCapture = AudioCapture()
    private let vad = VoiceActivityDetector()
    private let asrStage: AsrStage
    private let thermalMonitor: ThermalMonitor

    // Model paths
    private var asrModelPath: String?

    // Pipeline state
    private var isRunning = false
    private var pipelineTask: Task<Void, Never>?

    // Language pair
    private var srcLang: String = "zh"
    private var tgtLang: String = "en"

    init(messages: MessageStream) {
        self.messages = messages
        self.asrStage = AsrStage(messages: messages)
        self.thermalMonitor = ThermalMonitor(messages: messages)
    }

    /// Initialize with the ASR model package path.
    /// LLM is handled by the Dart-side llamadart service.
    func initialize(asrPath: String) {
        self.asrModelPath = asrPath

        os_log("[Pipeline] Initialized with ASR model: %{public}@", asrPath)

        Task {
            do {
                try await asrStage.loadModel(path: asrPath)
                messages.post(.engineReady(status: "ASR model loaded"))
            } catch {
                os_log("[Pipeline] ASR model load failed: %{public}@", error.localizedDescription)
                messages.post(.error(code: -3, detail: "ASR model load failed: \(error.localizedDescription)"))
            }
        }
    }

    /// Start the interpretation pipeline.
    ///
    /// Requests microphone permission (if not yet granted) before starting
    /// audio capture. On permission denial, posts an `.error` and leaves the
    /// pipeline in the stopped state — the UI should surface this to the user
    /// so they can enable the microphone in Settings.
    ///
    /// - Returns: `nil` on success; otherwise a human-readable error string
    ///            describing why the pipeline could not start.
    func start(srcLang: String, tgtLang: String) async -> String? {
        guard !isRunning else { return nil }
        guard asrModelPath != nil else {
            messages.post(.error(code: -1, detail: "Engine not initialized"))
            return "Engine not initialized"
        }

        self.srcLang = srcLang
        self.tgtLang = tgtLang

        asrStage.setLanguage(srcLang)
        thermalMonitor.start()

        os_log("[Pipeline] Requesting microphone permission…")

        let permResult = await audioCapture.requestPermission()
        switch permResult {
        case .granted:
            os_log("[Pipeline] Permission granted — starting audio capture")
            break
        case .denied:
            os_log("[Pipeline] Microphone permission denied")
            messages.post(.error(
                code: -4,
                detail: "Microphone permission denied. Enable it in Settings → Privacy → Microphone."))
            self.isRunning = false
            return "Microphone permission denied"
        case .sessionError(let error):
            os_log("[Pipeline] Audio session error: %{public}@",
                   error.localizedDescription)
            messages.post(.error(
                code: -5,
                detail: "Audio session setup failed: \(error.localizedDescription)"))
            self.isRunning = false
            return "Audio session setup failed: \(error.localizedDescription)"
        }

        os_log("[Pipeline] Starting: %{public}@ -> %{public}@", srcLang, tgtLang)
        self.isRunning = true

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
            // Always call stop() here — it resets the AVAudioEngine so the
            // NEXT start() attempt doesn't hit "invalid reuse after
            // initialization failure".
            audioCapture.stop()
            isRunning = false
            return "Audio capture failed: \(error.localizedDescription)"
        }

        return nil
    }

    /// Stop the interpretation pipeline.
    ///
    /// Any speech the user said that was still accumulating in the VAD
    /// buffer (not yet locked as a segment because they hadn't paused long
    /// enough) is flushed through ASR before teardown — otherwise pressing
    /// stop would silently discard whatever they just said.
    func stop() {
        guard isRunning else { return }

        os_log("[Pipeline] Stopping...")

        // 1. Stop pulling new audio from the mic.
        audioCapture.stop()

        // 2. Flush any pending VAD buffer through ASR. MUST happen while
        //    `isRunning == true` so processSegment() doesn't drop the
        //    segment, and before `vad.reset()` wipes the buffer.
        vad.flush()

        // 3. Tear down pipeline state.
        isRunning = false
        vad.reset()
        thermalMonitor.stop()

        // NOTE: do NOT `pipelineTask?.cancel()` here — the flushed segment
        // just kicked off an async ASR task, and we want it to complete so
        // the user sees what they said. The task posts its result via
        // `messages` regardless of pipeline state.

        // Deactivate the audio session so other apps can reclaim it.
        // `.notifyOthersOnDeactivation` keeps well-behaved with background
        // music/podcasts the user may be running alongside interpretation.
        do {
            try AVAudioSession.sharedInstance().setActive(
                false, options: .notifyOthersOnDeactivation)
        } catch {
            os_log("[Pipeline] Audio session deactivate failed: %{public}@",
                   error.localizedDescription)
        }

        os_log("[Pipeline] Stopped")
    }

    // MARK: - Segment Processing

    /// Process a locked audio segment through ASR.
    /// Confirmed text is forwarded to Dart for LLM translation.
    private func processSegment(_ segment: LockedSegment) {
        guard isRunning else { return }

        pipelineTask = Task.detached { [weak self] in
            guard let self = self else { return }

            let asrText = await self.runASR(segment: segment)
            guard !asrText.isEmpty else { return }

            self.messages.post(.asrConfirmed(
                speakerId: segment.speakerId,
                text: asrText,
                segmentId: segment.segmentId
            ))
        }
    }

    // MARK: - Debug Test Injection

    /// Inject a test ASR segment for simulator testing (no microphone required).
    /// Posts a fake `.asrPartial` immediately, then `.asrConfirmed` to trigger
    /// the LLM translation pipeline.
    func injectTestText(_ text: String, speakerId: Int = 0) {
        guard isRunning else {
            os_log("[Pipeline] Cannot inject test text — pipeline not running")
            return
        }
        let segmentId = Int.random(in: 1000...9999)
        os_log("[Pipeline] Injecting test text: %{public}@", text)
        messages.post(.asrPartial(
            speakerId: speakerId,
            text: text,
            segmentId: segmentId
        ))
        messages.post(.asrConfirmed(
            speakerId: speakerId,
            text: text,
            segmentId: segmentId
        ))
    }

    // MARK: - ASR Stage

    private func runASR(segment: LockedSegment) async -> String {
        await asrStage.recognize(segment: segment)
    }
}
