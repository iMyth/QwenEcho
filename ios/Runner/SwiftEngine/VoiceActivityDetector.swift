import Foundation

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
    private let noiseEnergyThreshold: Int32

    // State
    private enum State { case idle, accumulating }
    private var state: State = .idle

    // Accumulation buffers
    private var speechBuffer: [Int16] = []
    private var silenceDurationMs: Int = 0
    private var speechDurationMs: Int = 0
    private var segmentCounter: Int = 0

    // Callback
    private var onSegmentLocked: ((LockedSegment) -> Void)?

    // VAD frame size: 10ms at 16kHz = 160 samples
    private let frameSize: Int

    init(sampleRate: Int = 16000,
         silenceThresholdMs: Int = 400,
         minSpeechMs: Int = 200,
         maxSegmentMs: Int = 15000,
         noiseEnergyThreshold: Int32 = 150) {
        self.sampleRate = sampleRate
        self.silenceThresholdMs = silenceThresholdMs
        self.minSpeechMs = minSpeechMs
        self.maxSegmentMs = maxSegmentMs
        self.noiseEnergyThreshold = noiseEnergyThreshold
        self.frameSize = sampleRate / 100 // 10ms frames
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
    }

    /// Feed audio samples for VAD processing.
    /// Processes audio in 10ms frames.
    func feedAudio(_ samples: [Int16], speakerId: Int = 0) {
        var i = 0
        while i < samples.count {
            let remaining = samples.count - i
            let take = min(frameSize, remaining)
            let frame = Array(samples[i..<(i + take)])
            i += take

            // Compute energy for this frame
            let energy = computeEnergy(frame)
            let isSpeech = energy > noiseEnergyThreshold

            processFrame(frame, isSpeech: isSpeech, speakerId: speakerId)
        }
    }

    /// Notify that sentence-ending punctuation was detected by ASR.
    /// Forces an immediate segment lock if enough speech has accumulated.
    func notifyPunctuation(speakerId: Int) {
        guard state == .accumulating, speechDurationMs >= minSpeechMs else { return }
        lockSegment(speakerId: speakerId)
    }

    // MARK: - Private

    private func computeEnergy(_ samples: [Int16]) -> Int32 {
        guard !samples.isEmpty else { return 0 }
        var sum: Int64 = 0
        for s in samples {
            sum += abs(Int64(s))
        }
        return Int32(sum / Int64(samples.count))
    }

    private func processFrame(_ frame: [Int16], isSpeech: Bool, speakerId: Int) {
        let frameMs = 10

        switch state {
        case .idle:
            if isSpeech {
                state = .accumulating
                speechBuffer.append(contentsOf: frame)
                speechDurationMs = frameMs
                silenceDurationMs = 0
            }

        case .accumulating:
            speechBuffer.append(contentsOf: frame)

            if isSpeech {
                speechDurationMs += frameMs
                silenceDurationMs = 0
            } else {
                silenceDurationMs += frameMs

                // Check lock conditions
                if speechDurationMs >= minSpeechMs && silenceDurationMs >= silenceThresholdMs {
                    lockSegment(speakerId: speakerId)
                }
            }

            // Force-lock at max segment duration
            if speechDurationMs >= maxSegmentMs {
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
