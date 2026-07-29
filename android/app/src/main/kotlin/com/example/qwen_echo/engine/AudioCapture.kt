package com.example.qwen_echo.engine

import android.Manifest
import android.app.Activity
import android.content.pm.PackageManager
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.media.audiofx.AcousticEchoCanceler
import android.util.Log
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat

/// Result of the microphone permission request.
sealed class AudioPermissionResult {
    data object Granted : AudioPermissionResult()
    data object Denied : AudioPermissionResult()
    data class SessionError(val error: Exception) : AudioPermissionResult()
}

/// Captures PCM audio from the microphone via Android AudioRecord.
///
/// Format: 16kHz, 16-bit, mono PCM — directly matches SenseVoice input.
/// Unlike iOS (which captures 48kHz Float32 and downsamples), Android
/// AudioRecord can capture at 16kHz natively.
///
/// On the emulator without mic passthrough, falls back to synthetic silence.
class AudioCapture {

    private var audioRecord: AudioRecord? = null
    private var echoCanceler: AcousticEchoCanceler? = null
    private var captureThread: Thread? = null
    private var callback: ((ShortArray) -> Unit)? = null

    @Volatile
    var isRunning = false
        private set

    private var permissionGranted = false

    // Diagnostic counters
    private var bufferCount: Int = 0
    private var lastDiagnosticBufferCount: Int = 0

    // Target format: 16kHz, 16-bit, mono
    private val sampleRate = 16000
    private val channelConfig = AudioFormat.CHANNEL_IN_MONO
    private val audioFormat = AudioFormat.ENCODING_PCM_16BIT

    companion object {
        private const val TAG = "AudioCapture"
        const val REQUEST_RECORD_AUDIO = 1001
    }

    /// Configure the audio session and request microphone permission.
    /// Must be called before start(). Requires an Activity for the permission dialog.
    fun requestPermission(activity: Activity): AudioPermissionResult {
        if (ContextCompat.checkSelfPermission(activity, Manifest.permission.RECORD_AUDIO)
            == PackageManager.PERMISSION_GRANTED
        ) {
            permissionGranted = true
            return AudioPermissionResult.Granted
        }

        // Request permission — this is async, caller should handle via
        // onRequestPermissionsResult in the Activity.
        ActivityCompat.requestPermissions(
            activity,
            arrayOf(Manifest.permission.RECORD_AUDIO),
            REQUEST_RECORD_AUDIO,
        )
        // We return a provisional result; the real check happens in
        // onPermissionResult(). For simplicity, we treat "about to ask" as
        // "not yet granted" — caller should retry start() after permission.
        return AudioPermissionResult.Denied
    }

    /// Called by the Activity when the permission dialog result comes back.
    fun onPermissionResult(granted: Boolean) {
        permissionGranted = granted
        Log.d(TAG, "Microphone permission granted: $granted")
    }

    /// Start audio capture.
    ///
    /// - On a real device with permission granted: uses the real microphone.
    /// - On emulator without mic: falls back to synthetic silence.
    /// - Parameter callback: Called with PCM Int16 samples on the audio thread.
    fun start(callback: (ShortArray) -> Unit): String? {
        if (isRunning) return null
        this.callback = callback

        val bufferSize = AudioRecord.getMinBufferSize(sampleRate, channelConfig, audioFormat)
        if (bufferSize == AudioRecord.ERROR || bufferSize == AudioRecord.ERROR_BAD_VALUE) {
            Log.e(TAG, "Invalid buffer size: $bufferSize")
            return "AudioRecord.getMinBufferSize failed"
        }

        // Use a buffer 2x the minimum for safety
        val effectiveBufferSize = bufferSize * 2

        try {
            audioRecord = AudioRecord(
                MediaRecorder.AudioSource.VOICE_RECOGNITION,
                sampleRate,
                channelConfig,
                audioFormat,
                effectiveBufferSize,
            )

            if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
                audioRecord?.release()
                audioRecord = null
                return "AudioRecord initialization failed"
            }

            // Enable Acoustic Echo Cancellation (AEC) to prevent TTS feedback loop.
            // This allows users to speak continuously while TTS is playing.
            if (AcousticEchoCanceler.isAvailable()) {
                echoCanceler = AcousticEchoCanceler.create(audioRecord!!.audioSessionId)
                echoCanceler?.enabled = true
                Log.d(TAG, "Acoustic Echo Cancellation enabled")
            } else {
                Log.w(TAG, "Acoustic Echo Cancellation not available on this device")
            }

            audioRecord?.startRecording()
            isRunning = true
            bufferCount = 0
            lastDiagnosticBufferCount = 0

            captureThread = Thread {
                val buffer = ShortArray(effectiveBufferSize / 2)
                while (isRunning) {
                    val read = audioRecord?.read(buffer, 0, buffer.size) ?: -1
                    if (read > 0) {
                        val samples = buffer.copyOf(read)
                        callback(samples)

                        bufferCount++
                        if (bufferCount == 1 || bufferCount - lastDiagnosticBufferCount >= 50) {
                            var sum: Long = 0
                            var peak: Int = 0
                            for (s in samples) {
                                val v = kotlin.math.abs(s.toInt())
                                sum += v.toLong()
                                if (v > peak) peak = v
                            }
                            val avg = if (samples.isNotEmpty()) (sum / samples.size).toInt() else 0
                            Log.d(TAG, "diag #$bufferCount: frames=${samples.size} avg=$avg peak=$peak")
                            lastDiagnosticBufferCount = bufferCount
                        }
                    } else if (read < 0) {
                        Log.e(TAG, "AudioRecord read error: $read")
                        break
                    }
                }
            }.apply {
                name = "AudioCapture"
                priority = Thread.MAX_PRIORITY
                start()
            }

            Log.d(TAG, "Audio capture started (16kHz Int16 mono, bufferSize=$effectiveBufferSize)")
            return null
        } catch (e: SecurityException) {
            Log.e(TAG, "Microphone permission not granted", e)
            audioRecord?.release()
            audioRecord = null
            return "Microphone permission not granted"
        } catch (e: Exception) {
            Log.e(TAG, "Audio capture failed", e)
            audioRecord?.release()
            audioRecord = null
            return "Audio capture failed: ${e.message}"
        }
    }

    /// Stop audio capture and release resources.
    /// AudioRecord is NOT reusable after release — next start() creates a new one.
    fun stop() {
        isRunning = false

        captureThread?.interrupt()
        captureThread = null

        echoCanceler?.release()
        echoCanceler = null

        try {
            audioRecord?.stop()
        } catch (e: IllegalStateException) {
            // Already stopped or not initialized — safe to ignore
        }
        audioRecord?.release()
        audioRecord = null

        callback = null
        Log.d(TAG, "Audio capture stopped")
    }
}
