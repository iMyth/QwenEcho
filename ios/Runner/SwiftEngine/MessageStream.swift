import Foundation

/// Thread-safe message stream that buffers messages for Flutter consumption.
///
/// The pipeline produces messages from multiple threads (audio capture, ASR, LLM).
/// Flutter consumes them via EventChannel on the main thread.
/// This class bridges the gap with a lock-free queue + FlutterEventSink.
final class MessageStream {

    private let queue = DispatchQueue(label: "com.qwenecho.messagestream", attributes: .concurrent)
    private var buffer: [EchoMessage] = []
    private var sink: FlutterEventSink?

    /// Set the Flutter event sink. Call when EventChannel starts listening.
    func setSink(_ sink: @escaping FlutterEventSink) {
        queue.async(flags: .barrier) {
            self.sink = sink
            // Flush any buffered messages
            for msg in self.buffer {
                sink(msg.toMap())
            }
            self.buffer.removeAll()
        }
    }

    /// Clear the sink. Call when EventChannel is cancelled.
    func clearSink() {
        queue.async(flags: .barrier) {
            self.sink = nil
        }
    }

    /// Post a message. If a sink is attached, deliver immediately.
    /// Otherwise buffer for later delivery.
    func post(_ message: EchoMessage) {
        queue.async(flags: .barrier) {
            if let sink = self.sink {
                DispatchQueue.main.async {
                    sink(message.toMap())
                }
            } else {
                self.buffer.append(message)
                // Cap buffer to prevent unbounded growth
                if self.buffer.count > 100 {
                    self.buffer.removeFirst(self.buffer.count - 100)
                }
            }
        }
    }
}
