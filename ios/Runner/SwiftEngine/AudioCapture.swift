import Foundation
import AVFAudio

/// Captures PCM audio from the microphone via AVAudioEngine.
///
/// Format: 16kHz, 16-bit, mono PCM.
/// Delivers audio samples via a callback on the audio thread.
final class AudioCapture {

    private let engine = AVAudioEngine()
    private var converter: AVAudioConverter?
    private var callback: (([Int16]) -> Void)?

    private(set) var isRunning = false

    // Target format: 16kHz, 16-bit, mono
    private let targetFormat = AVAudioFormat(
        commonFormat: .pcmFormatInt16,
        sampleRate: 16000,
        channels: 1,
        interleaved: false
    )!

    /// Start audio capture.
    /// - Parameter callback: Called with PCM Int16 samples on the audio thread.
    func start(callback: @escaping ([Int16]) -> Void) throws {
        guard !isRunning else { return }
        self.callback = callback

        let inputNode = engine.inputNode
        let inputFormat = inputNode.outputFormat(forBus: 0)

        // Create converter from input format to target format
        guard let converter = AVAudioConverter(from: inputFormat, to: targetFormat) else {
            throw NSError(domain: "AudioCapture", code: 1,
                          userInfo: [NSLocalizedDescriptionKey: "Cannot create audio converter"])
        }
        self.converter = converter

        inputNode.removeTap(onBus: 0)
        inputNode.installTap(onBus: 0, bufferSize: 1024, format: inputFormat) { [weak self] buffer, _ in
            self?.processBuffer(buffer)
        }

        engine.prepare()
        try engine.start()
        isRunning = true
    }

    /// Stop audio capture.
    func stop() {
        guard isRunning else { return }
        engine.inputNode.removeTap(onBus: 0)
        engine.stop()
        isRunning = false
        callback = nil
    }

    /// Convert AVAudioPCMBuffer to Int16 samples and deliver via callback.
    private func processBuffer(_ inputBuffer: AVAudioPCMBuffer) {
        guard let converter = converter else { return }

        let targetFormat = self.targetFormat

        // Calculate output frame capacity
        let ratio = targetFormat.sampleRate / inputBuffer.format.sampleRate
        let outputCapacity = AVAudioFrameCount(Double(inputBuffer.frameLength) * ratio) + 1024

        guard let outputBuffer = AVAudioPCMBuffer(pcmFormat: targetFormat, frameCapacity: outputCapacity) else {
            return
        }

        var error: NSError?
        var conversionDone = false

        converter.convert(to: outputBuffer, error: &error) { _, status in
            if conversionDone {
                status.pointee = .endOfStream
                return nil
            }
            conversionDone = true
            status.pointee = .haveData
            return inputBuffer
        }

        if error != nil { return }

        // Extract Int16 samples
        let frameLength = Int(outputBuffer.frameLength)
        guard let int16Channel = outputBuffer.int16ChannelData?[0] else { return }

        let samples = Array(UnsafeBufferPointer(start: int16Channel, count: frameLength))
        callback?(samples)
    }
}
