package com.example.qwen_echo.engine

import android.os.Handler
import android.os.Looper
import android.util.Log
import io.flutter.plugin.common.EventChannel
import java.util.concurrent.ConcurrentLinkedQueue

/// Thread-safe message stream that buffers messages for Flutter consumption.
///
/// The pipeline produces messages from multiple threads (audio capture, ASR).
/// Flutter consumes them via EventChannel on the main thread.
/// This class bridges the gap: posts from any thread, delivers on main thread.
///
/// Mirrors iOS MessageStream (DispatchQueue + barrier pattern).
class MessageStream {

    private val mainHandler = Handler(Looper.getMainLooper())
    private var sink: EventChannel.EventSink? = null
    private val buffer = ConcurrentLinkedQueue<EchoMessage>()
    private val maxBufferSize = 100

    /// Set the Flutter event sink. Call when EventChannel starts listening.
    /// Flushes any buffered messages immediately.
    @Synchronized
    fun setSink(sink: EventChannel.EventSink?) {
        this.sink = sink
        // Flush buffered messages
        while (true) {
            val msg = buffer.poll() ?: break
            deliver(msg)
        }
    }

    /// Clear the sink. Call when EventChannel is cancelled.
    @Synchronized
    fun clearSink() {
        this.sink = null
    }

    /// Post a message. If a sink is attached, deliver immediately on main thread.
    /// Otherwise buffer for later delivery (capped at [maxBufferSize]).
    fun post(message: EchoMessage) {
        mainHandler.post {
            val currentSink = synchronized(this) { sink }
            if (currentSink != null) {
                deliver(message)
            } else {
                buffer.add(message)
                // Cap buffer to prevent unbounded growth
                while (buffer.size > maxBufferSize) {
                    buffer.poll()
                }
            }
        }
    }

    private fun deliver(message: EchoMessage) {
        val currentSink = synchronized(this) { sink } ?: return
        try {
            currentSink.success(message.toMap())
        } catch (e: Exception) {
            Log.w(TAG, "Failed to deliver message to Flutter: ${e.message}")
        }
    }

    companion object {
        private const val TAG = "MessageStream"
    }
}
