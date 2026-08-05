import Foundation
import os

/// A locked audio segment ready for ASR processing.
struct LockedSegment {
    let audioData: [Int16]
    let segmentId: Int
    let speakerId: Int
    let timestampMs: Int64
}

/// Voice Activity Detector + Sentence Segmenter.
///
/// Classifies audio frames as speech/non-speech using energy-based VAD
/// and determines sentence boundaries via a state machine.
///
/// State machine:
///   Idle -> Accumulating (on speech onset)
///   Accumulating -> Locking (on 400ms silence, punctuation, or 15s force-lock)
///   Locking -> Idle (segment dispatched)
final class VoiceActivityDetector {

    // Configuration
    private let sampleRate: Int
    private let silenceThresholdMs: Int
    private let minSpeechMs: Int
    private let maxSegmentMs: Int
    private let baseNoiseEnergyThreshold: Int32

    // Dynamic threshold for TTS echo cancellation
    // When TTS is playing, we increase the threshold to avoid picking up TTS output
    private var currentNoiseEnergyThreshold: Int32

    // State
    private enum State: CustomStringConvertible { case idle, accumulating
        var description: String {
            switch self { case .idle: "idle"; case .accumulating: "accumulating" }
        }
    }
    private var state: State = .idle

    // Accumulation buffers
    private var speechBuffer: [Int16] = []
    private var silenceDurationMs: Int = 0
    private var speechDurationMs: Int = 0
    private var segmentCounter: Int = 0

    // Diagnostic counters
    private var frameCount: Int = 0
    private var lastDiagFrameCount: Int = 0

    // Callback
    private var onSegmentLocked: ((LockedSegment) -> Void)?

    // VAD frame size: 10ms at 16kHz = 160 samples
    private let frameSize: Int

    init(sampleRate: Int = 16000,
         silenceThresholdMs: Int = 400,
         minSpeechMs: Int = 300,
         maxSegmentMs: Int = 8000,
         noiseEnergyThreshold: Int32 = 500) {
        self.sampleRate = sampleRate
        self.silenceThresholdMs = silenceThresholdMs
        self.minSpeechMs = minSpeechMs
        self.maxSegmentMs = maxSegmentMs
        self.baseNoiseEnergyThreshold = noiseEnergyThreshold
        self.currentNoiseEnergyThreshold = noiseEnergyThreshold
        self.frameSize = sampleRate / 100 // 10ms frames
    }

    /// Set threshold multiplier for dynamic sensitivity adjustment.
    /// Used during TTS playback to avoid picking up TTS output as speech.
    /// - Parameter multiplier: 1.0 = normal, >1.0 = less sensitive (e.g., 2.5)
    func setThresholdMultiplier(_ multiplier: Float) {
        currentNoiseEnergyThreshold = Int32(Float(baseNoiseEnergyThreshold) * multiplier)
        os_log("[VAD] Threshold multiplier set to %.1f (base=%d, current=%d)",
               multiplier, Int(baseNoiseEnergyThreshold), Int(currentNoiseEnergyThreshold))
    }

    /// Set the callback invoked when a segment is locked.
    func setSegmentCallback(_ callback: @escaping (LockedSegment) -> Void) {
        self.onSegmentLocked = callback
    }

    /// Reset the detector to idle state, discarding any accumulated audio.
    func reset() {
        state = .idle
        speechBuffer.removeAll()
        silenceDurationMs = 0
        speechDurationMs = 0
        frameCount = 0
        lastDiagFrameCount = 0
    }

    /// Flush any accumulated speech as a final segment.
    ///
    /// Called when the pipeline is being stopped — without this, any audio
    /// the user spoke just before pressing stop would be silently discarded
    /// by [reset]. If at least [minSpeechMs] of speech has accumulated, lock
    /// it as a segment so it can still be transcribed.
    func flush(speakerId: Int = 0) {
        guard state == .accumulating, speechDurationMs >= minSpeechMs else {
            // Either no speech accumulated, or too short to be useful.
            state = .idle
            speechBuffer.removeAll()
            silenceDurationMs = 0
            speechDurationMs = 0
            return
        }
        os_log("[VAD] 🔒 Flush-locking final segment on stop: speech=%dms",
               speechDurationMs)
        lockSegment(speakerId: speakerId)
    }

    /// Feed audio samples for VAD processing.
    /// Processes audio in 10ms frames.
    ///
    /// Performance: processes frames in-place without allocating per-frame
    /// arrays. The previous implementation called `Array(samples[i..<(i+take)])`
    /// ~100×/sec on the audio thread, causing allocation churn that could
    /// contribute to audio glitches.
    func feedAudio(_ samples: [Int16], speakerId: Int = 0) {
        var i = 0
        while i < samples.count {
            let remaining = samples.count - i
            let take = min(frameSize, remaining)
            let frameEnd = i + take

            // Compute energy directly on the slice (no allocation)
            let energy = computeEnergy(samples, from: i, to: frameEnd)
            let isSpeech = energy > currentNoiseEnergyThreshold

            frameCount += 1

            // Diagnostic: first frame + every 200 frames (~2s) + every
            // state transition (logged in processFrame).
            if frameCount == 1 || frameCount - lastDiagFrameCount >= 200 {
                os_log("[VAD] diag #%d: energy=%d thresh=%d speech=%{public}@ state=%{public}@",
                       frameCount, Int(energy), Int(currentNoiseEnergyThreshold),
                       String(isSpeech), String(describing: state))
                lastDiagFrameCount = frameCount
            }

            processFrame(samples, from: i, to: frameEnd, isSpeech: isSpeech, speakerId: speakerId)
            i = frameEnd
        }
    }

    /// Notify that sentence-ending punctuation was detected by ASR.
    /// Forces an immediate segment lock if enough speech has accumulated.
    func notifyPunctuation(speakerId: Int) {
        guard state == .accumulating, speechDurationMs >= minSpeechMs else { return }
        lockSegment(speakerId: speakerId)
    }

    // MARK: - Private

    private func computeEnergy(_ samples: [Int16], from start: Int, to end: Int) -> Int32 {
        let count = end - start
        guard count > 0 else { return 0 }
        var sum: Int64 = 0
        for idx in start..<end {
            sum += abs(Int64(samples[idx]))
        }
        return Int32(sum / Int64(count))
    }

    private func processFrame(_ samples: [Int16], from start: Int, to end: Int, isSpeech: Bool, speakerId: Int) {
        let frameMs = 10
        let range = start..<end

        switch state {
        case .idle:
            if isSpeech {
                state = .accumulating
                os_log("[VAD] ▶ Speech onset (energy above threshold)")
                speechBuffer.append(contentsOf: samples[range])
                speechDurationMs = frameMs
                silenceDurationMs = 0
            }

        case .accumulating:
            speechBuffer.append(contentsOf: samples[range])

            if isSpeech {
                speechDurationMs += frameMs
                silenceDurationMs = 0
            } else {
                silenceDurationMs += frameMs

                // Check lock conditions
                if speechDurationMs >= minSpeechMs && silenceDurationMs >= silenceThresholdMs {
                    os_log("[VAD] 🔒 Locking segment: speech=%dms silence=%dms",
                           speechDurationMs, silenceDurationMs)
                    lockSegment(speakerId: speakerId)
                }
            }

            // Force-lock at max segment duration
            if speechDurationMs >= maxSegmentMs {
                os_log("[VAD] 🔒 Force-locking segment at max duration %dms",
                       speechDurationMs)
                lockSegment(speakerId: speakerId)
            }
        }
    }

    private func lockSegment(speakerId: Int) {
        guard !speechBuffer.isEmpty else {
            state = .idle
            return
        }

        segmentCounter += 1
        let segment = LockedSegment(
            audioData: speechBuffer,
            segmentId: segmentCounter,
            speakerId: speakerId,
            timestampMs: Int64(Date().timeIntervalSince1970 * 1000)
        )

        state = .idle
        speechBuffer.removeAll()
        silenceDurationMs = 0
        speechDurationMs = 0

        onSegmentLocked?(segment)
    }
}
