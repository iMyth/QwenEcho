import Foundation
import AVFAudio
import os

/// Result of the microphone permission request.
enum AudioPermissionResult {
    /// Permission was granted; audio session is active and ready.
    case granted
    /// User denied microphone permission.
    case denied
    /// Audio session configuration failed (unrelated to user choice).
    case sessionError(Error)
}

/// Captures PCM audio from the microphone via AVAudioEngine.
///
/// Format: 16kHz, 16-bit, mono PCM.
/// Delivers audio samples via a callback on the audio thread.
///
/// On the iOS simulator (no real microphone), falls back to synthetic
/// silence so the pipeline can start without crashing.
final class AudioCapture {

    private let engine = AVAudioEngine()
    private var callback: (([Int16]) -> Void)?
    private var silenceTimer: Timer?

    private(set) var isRunning = false
    private var permissionGranted = false

    // Diagnostic counters — track whether the tap is actually delivering
    // audio and at what level. Helps distinguish "no data" from "silence"
    // from "speech filtered by VAD" when debugging on the simulator.
    private var bufferCount: Int = 0
    private var lastDiagnosticBufferCount: Int = 0

    // Target format: 16kHz, 16-bit, mono
    private let targetFormat = AVAudioFormat(
        commonFormat: .pcmFormatInt16,
        sampleRate: 16000,
        channels: 1,
        interleaved: false
    )!

    /// Configure the audio session and request microphone permission.
    ///
    /// Must be called before [start]. Returns the outcome so the caller
    /// can distinguish "user denied" from "no hardware" (simulator).
    func requestPermission() async -> AudioPermissionResult {
        let session = AVAudioSession.sharedInstance()
        do {
            // `.record` is sufficient for ASR-only operation. Switch to
            // `.playAndRecord` once TTS output (Phase 5) is wired up.
            try session.setCategory(.record, mode: .default, options: [])
            try session.setActive(true, options: [])
        } catch {
            os_log("[AudioCapture] Audio session setup failed: %{public}@",
                   error.localizedDescription)
            return .sessionError(error)
        }

        return await withCheckedContinuation { continuation in
            session.requestRecordPermission { granted in
                os_log("[AudioCapture] Microphone permission granted: %{public}@",
                       String(granted))
                self.permissionGranted = granted
                continuation.resume(returning: granted ? .granted : .denied)
            }
        }
    }

    /// Start audio capture.
    ///
    /// Behavior:
    /// - On a real device (permission granted via [requestPermission]):
    ///   uses the real microphone.
    /// - On the iOS Simulator with Mac microphone passthrough enabled
    ///   (Simulator menu: Features → Audio Input → <Mac mic>):
    ///   uses the Mac's microphone as if it were the device mic.
    /// - On the iOS Simulator without passthrough configured:
    ///   falls back to synthetic silence so the pipeline can still run
    ///   (useful for `testInject()` debugging).
    ///
    /// - Parameter callback: Called with PCM Int16 samples on the audio thread.
    /// - Throws: If the microphone cannot be opened on a real device.
    func start(callback: @escaping ([Int16]) -> Void) throws {
        guard !isRunning else { return }
        self.callback = callback

        // Real device: permission must have been granted up front.
        // Simulator: permission request is a no-op (always "granted"); we
        // rely on inputFormat inspection below to detect whether a real
        // audio source is wired up.
        #if !targetEnvironment(simulator)
        guard permissionGranted else {
            throw NSError(domain: "AudioCapture", code: 2,
                          userInfo: [NSLocalizedDescriptionKey:
                                     "Microphone permission not granted"])
        }
        #endif

        // AVAudioEngine is not reusable after a failed `start()` — it must
        // be `reset()` before another attempt. Since we keep a single engine
        // instance across start/stop cycles, always reset up front to clear
        // any leftover failure state from a previous call.
        engine.reset()

        let inputNode = engine.inputNode
        let inputFormat = inputNode.outputFormat(forBus: 0)

        let hasValidInput = inputFormat.sampleRate > 0 && inputFormat.channelCount > 0

        if hasValidInput {
            // Audio input is available — use it. This path is taken on:
            //   • real device (permission granted in requestPermission())
            //   • iOS Simulator with Mac microphone passthrough enabled
            //
            // IMPORTANT: `installTap(format:)` must exactly match the input
            // node's format (sample rate, channel count, AND data type).
            // AVAudioEngine does NOT perform any conversion for taps — any
            // mismatch throws "Failed to create tap due to format mismatch".
            // So we install the tap with the native input format (typically
            // 48 kHz Float32 on the simulator/Mac passthrough path) and do
            // all conversion inside `processBuffer`.

            inputNode.removeTap(onBus: 0)
            inputNode.installTap(onBus: 0, bufferSize: 1024, format: inputFormat) { [weak self] buffer, _ in
                self?.processBuffer(buffer)
            }

            engine.prepare()
            do {
                try engine.start()
            } catch {
                os_log("[AudioCapture] engine.start() failed: %{public}@",
                       error.localizedDescription)
                inputNode.removeTap(onBus: 0)
                engine.reset()
                throw error
            }
            isRunning = true
            bufferCount = 0
            lastDiagnosticBufferCount = 0
            os_log("[AudioCapture] Real audio capture started (rate=%.0f, ch=%d, format=%@)).",
                   inputFormat.sampleRate, Int(inputFormat.channelCount),
                   inputFormat.commonFormat == .pcmFormatFloat32 ? "Float32" : "other")
        } else {
            // No audio input hardware available.
            #if targetEnvironment(simulator)
            // Simulator without Mac mic passthrough — degrade gracefully
            // to synthetic silence so the pipeline can still run for
            // debugging via testInject().
            os_log("[AudioCapture] No audio input on simulator. Using synthetic silence. (Tip: Simulator menu → Features → Audio Input → enable your Mac's mic.)")
            startSyntheticSilence()
            isRunning = true
            #else
            // Real device but input node reports no format even though
            // permission was granted — mic hardware or session problem.
            engine.reset()
            throw NSError(domain: "AudioCapture", code: 3,
                          userInfo: [NSLocalizedDescriptionKey:
                                     "Microphone hardware unavailable despite permission being granted"])
            #endif
        }
    }

    /// Stop audio capture.
    ///
    /// Always resets the engine — even when [isRunning] is false — so a prior
    /// failed [start] does not leave the engine in a poisoned state that
    /// would cause "invalid reuse after initialization failure" on the next
    /// attempt.
    func stop() {
        if silenceTimer != nil {
            silenceTimer?.invalidate()
            silenceTimer = nil
        } else if isRunning {
            engine.inputNode.removeTap(onBus: 0)
            engine.stop()
        }

        engine.reset()

        isRunning = false
        callback = nil
    }

    // MARK: - Synthetic Silence (Simulator Fallback)

    /// Generate periodic silence buffers on the main thread.
    /// This keeps the pipeline alive on the simulator without real audio.
    private func startSyntheticSilence() {
        // Deliver 1024 silence samples (~64ms at 16kHz) every 64ms.
        let samplesPerBuffer = 1024
        let interval = Double(samplesPerBuffer) / 16000.0 // ~0.064s

        silenceTimer = Timer.scheduledTimer(withTimeInterval: interval, repeats: true) { [weak self] _ in
            self?.callback?(Array(repeating: 0, count: samplesPerBuffer))
        }
        // Fire immediately for the first buffer.
        RunLoop.current.add(silenceTimer!, forMode: .common)
    }

    // MARK: - Real Audio Processing

    /// Read samples from the tap buffer, convert to 16 kHz Int16 mono, and
    /// deliver via callback.
    ///
    /// The tap is installed with the input node's native format (typically
    /// 48 kHz Float32 on the simulator/Mac passthrough path). We must handle
    /// two things here:
    ///   1. Data type conversion: Float32 [-1.0, 1.0] → Int16 [-32768, 32767]
    ///      (or Int16 passthrough if the hardware already gives Int16).
    ///   2. Sample-rate conversion: 48 kHz → 16 kHz (downsample by 3) via
    ///      linear interpolation. This is plenty for ASR — SenseVoice is
    ///      trained on telephone-quality 16 kHz audio.
    ///
    /// Both conversions happen in a single pass to keep the audio thread
    /// cheap and allocation-free (aside from the output buffer).
    private func processBuffer(_ inputBuffer: AVAudioPCMBuffer) {
        let frameLength = Int(inputBuffer.frameLength)
        guard frameLength > 0 else { return }

        let inRate = inputBuffer.format.sampleRate
        let outRate = targetFormat.sampleRate
        guard inRate > 0, outRate > 0 else { return }

        let ratio = inRate / outRate
        let outputFrames = Int(Double(frameLength) / ratio)
        guard outputFrames > 0 else { return }

        var samples = [Int16]()
        samples.reserveCapacity(outputFrames)

        // Path A: Float32 input (simulator / Mac passthrough / most modern iOS).
        if let floatChannel = inputBuffer.floatChannelData?[0] {
            for i in 0..<outputFrames {
                let srcIndex = Double(i) * ratio
                let idx0 = Int(srcIndex)
                let frac = Float(srcIndex - Double(idx0))

                let value: Float
                if idx0 + 1 < frameLength {
                    let s0 = floatChannel[idx0]
                    let s1 = floatChannel[idx0 + 1]
                    value = s0 + frac * (s1 - s0)
                } else if idx0 < frameLength {
                    value = floatChannel[idx0]
                } else {
                    continue
                }
                // Clamp + scale Float32 [-1, 1] → Int16.
                let scaled = Int(value * 32767.0)
                samples.append(Int16(max(-32768, min(32767, scaled))))
            }
        }
        // Path B: Int16 input (some real devices).
        else if let int16Channel = inputBuffer.int16ChannelData?[0] {
            for i in 0..<outputFrames {
                let srcIndex = Double(i) * ratio
                let idx0 = Int(srcIndex)
                let frac = Float(srcIndex - Double(idx0))

                let value: Int16
                if idx0 + 1 < frameLength {
                    let s0 = Float(int16Channel[idx0])
                    let s1 = Float(int16Channel[idx0 + 1])
                    value = Int16(max(-32768, min(32767, Int(s0 + frac * (s1 - s0)))))
                } else if idx0 < frameLength {
                    value = int16Channel[idx0]
                } else {
                    continue
                }
                samples.append(value)
            }
        } else {
            // Unsupported format — log and skip.
            os_log("[AudioCapture] buffer has neither float nor Int16 data — skipping")
            return
        }

        // Diagnostic: confirm buffers are flowing and at what energy level.
        // First buffer logged unconditionally; then every 50 buffers (~3s)
        // we log min/max/avg so we can tell silence from speech.
        bufferCount += 1
        if bufferCount == 1 {
            os_log("[AudioCapture] ✓ First buffer received: %d in → %d out samples (%.0f→%.0f Hz).",
                   frameLength, samples.count, inRate, outRate)
        }
        if bufferCount == 1 || bufferCount - lastDiagnosticBufferCount >= 50 {
            var sum: Int64 = 0
            var peak: Int16 = 0
            for s in samples {
                let a = abs(Int32(s))
                sum += Int64(a)
                if a > abs(Int32(peak)) { peak = s }
            }
            let avg = samples.isEmpty ? 0 : Int(sum / Int64(samples.count))
            os_log("[AudioCapture] diag #%d: frames=%d avg=%d peak=%d",
                   bufferCount, samples.count, avg, Int(abs(Int32(peak))))
            lastDiagnosticBufferCount = bufferCount
        }

        callback?(samples)
    }
}
