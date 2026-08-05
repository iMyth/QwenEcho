package com.myth.qwenecho.engine

import android.util.Log
import kotlin.math.abs

/// Voice Activity Detector + Sentence Segmenter.
///
/// 1:1 port of iOS VoiceActivityDetector.swift.
/// Classifies audio frames as speech/non-speech using energy-based VAD
/// and determines sentence boundaries via a state machine.
///
/// State machine:
///   Idle -> Accumulating (on speech onset)
///   Accumulating -> Locking (on 400ms silence, punctuation, or 8s force-lock)
///   Locking -> Idle (segment dispatched)
class VoiceActivityDetector(
    private val sampleRate: Int = 16000,
    private val silenceThresholdMs: Int = 400,
    private val minSpeechMs: Int = 300,
    private val maxSegmentMs: Int = 8000,
    private val noiseEnergyThreshold: Int = 500,
) {
    private enum class State { IDLE, ACCUMULATING }

    private var state: State = State.IDLE

    // Accumulation buffers
    private val speechBuffer = mutableListOf<Short>()
    private var silenceDurationMs: Int = 0
    private var speechDurationMs: Int = 0
    private var segmentCounter: Int = 0

    // Diagnostic counters
    private var frameCount: Int = 0
    private var lastDiagFrameCount: Int = 0

    // Callback
    private var onSegmentLocked: ((LockedSegment) -> Unit)? = null

    // VAD frame size: 10ms at 16kHz = 160 samples
    private val frameSize: Int = sampleRate / 100

    companion object {
        private const val TAG = "VAD"
    }

    /// Set the callback invoked when a segment is locked.
    fun setSegmentCallback(callback: (LockedSegment) -> Unit) {
        this.onSegmentLocked = callback
    }

    /// Reset the detector to idle state, discarding any accumulated audio.
    fun reset() {
        state = State.IDLE
        speechBuffer.clear()
        silenceDurationMs = 0
        speechDurationMs = 0
        frameCount = 0
        lastDiagFrameCount = 0
    }

    /// Flush any accumulated speech as a final segment.
    ///
    /// Called when the pipeline is being stopped — without this, any audio
    /// the user spoke just before pressing stop would be silently discarded
    /// by reset(). If at least minSpeechMs of speech has accumulated, lock
    /// it as a segment so it can still be transcribed.
    fun flush(speakerId: Int = 0) {
        if (state != State.ACCUMULATING || speechDurationMs < minSpeechMs) {
            state = State.IDLE
            speechBuffer.clear()
            silenceDurationMs = 0
            speechDurationMs = 0
            return
        }
        Log.d(TAG, "Flush-locking final segment on stop: speech=${speechDurationMs}ms")
        lockSegment(speakerId)
    }

    /// Feed audio samples for VAD processing.
    /// Processes audio in 10ms frames.
    fun feedAudio(samples: ShortArray, speakerId: Int = 0) {
        var i = 0
        while (i < samples.size) {
            val remaining = samples.size - i
            val take = minOf(frameSize, remaining)
            val frame = samples.copyOfRange(i, i + take)
            i += take

            // Compute energy for this frame
            val energy = computeEnergy(frame)
            val isSpeech = energy > noiseEnergyThreshold

            frameCount++

            // Diagnostic: first frame + every 200 frames (~2s)
            if (frameCount == 1 || frameCount - lastDiagFrameCount >= 200) {
                Log.d(TAG, "diag #$frameCount: energy=$energy thresh=$noiseEnergyThreshold speech=$isSpeech state=$state")
                lastDiagFrameCount = frameCount
            }

            processFrame(frame, isSpeech, speakerId)
        }
    }

    /// Notify that sentence-ending punctuation was detected by ASR.
    /// Forces an immediate segment lock if enough speech has accumulated.
    fun notifyPunctuation(speakerId: Int) {
        if (state != State.ACCUMULATING || speechDurationMs < minSpeechMs) return
        lockSegment(speakerId)
    }

    // MARK: - Private

    private fun computeEnergy(samples: ShortArray): Int {
        if (samples.isEmpty()) return 0
        var sum: Long = 0
        for (s in samples) {
            sum += abs(s.toLong())
        }
        return (sum / samples.size).toInt()
    }

    private fun processFrame(frame: ShortArray, isSpeech: Boolean, speakerId: Int) {
        val frameMs = 10

        when (state) {
            State.IDLE -> {
                if (isSpeech) {
                    state = State.ACCUMULATING
                    Log.d(TAG, "Speech onset (energy above threshold)")
                    speechBuffer.addAll(frame.toList())
                    speechDurationMs = frameMs
                    silenceDurationMs = 0
                }
            }

            State.ACCUMULATING -> {
                speechBuffer.addAll(frame.toList())

                if (isSpeech) {
                    speechDurationMs += frameMs
                    silenceDurationMs = 0
                } else {
                    silenceDurationMs += frameMs

                    // Check lock conditions
                    if (speechDurationMs >= minSpeechMs && silenceDurationMs >= silenceThresholdMs) {
                        Log.d(TAG, "Locking segment: speech=${speechDurationMs}ms silence=${silenceDurationMs}ms")
                        lockSegment(speakerId)
                    }
                }

                // Force-lock at max segment duration
                if (speechDurationMs >= maxSegmentMs) {
                    Log.d(TAG, "Force-locking segment at max duration ${speechDurationMs}ms")
                    lockSegment(speakerId)
                }
            }
        }
    }

    private fun lockSegment(speakerId: Int) {
        if (speechBuffer.isEmpty()) {
            state = State.IDLE
            return
        }

        segmentCounter++
        val segment = LockedSegment(
            audioData = speechBuffer.toShortArray(),
            segmentId = segmentCounter,
            speakerId = speakerId,
            timestampMs = System.currentTimeMillis(),
        )

        state = State.IDLE
        speechBuffer.clear()
        silenceDurationMs = 0
        speechDurationMs = 0

        onSegmentLocked?.invoke(segment)
    }
}
