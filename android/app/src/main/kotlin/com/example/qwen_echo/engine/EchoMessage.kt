package com.example.qwen_echo.engine

import android.util.Log

/// Message types flowing through the interpretation pipeline.
/// Serialized as dictionaries and sent to Flutter via EventChannel.
/// Must match iOS EchoMessageType raw values exactly.
enum class EchoMessageType(val value: Int) {
    ASR_PARTIAL(0),
    ASR_CONFIRMED(1),
    TRANSLATION_STREAM(2),
    TRANSLATION_DONE(3),
    ERROR(4),
    THERMAL_STATE(5),
    LATENCY_WARNING(6),
    ENGINE_READY(7);
}

/// A locked audio segment ready for ASR processing.
data class LockedSegment(
    val audioData: ShortArray,
    val segmentId: Int,
    val speakerId: Int,
    val timestampMs: Long,
)

/// A typed message that can be sent to Flutter via EventChannel.
/// toMap() output must match iOS EchoMessage.toMap() exactly.
data class EchoMessage(
    val type: EchoMessageType,
    val speakerId: Int = 0,
    val text: String = "",
    val segmentId: Int = 0,
    val timestampMs: Long = System.currentTimeMillis(),
    val errorCode: Int = 0,
    val detail: String = "",
) {
    /// Convert to a dictionary for FlutterEventSink.
    /// Keys and types must match iOS exactly.
    fun toMap(): Map<String, Any> = mapOf(
        "type" to type.value,
        "speakerId" to speakerId,
        "text" to text,
        "segmentId" to segmentId,
        "timestampMs" to timestampMs,
        "errorCode" to errorCode,
        "detail" to detail,
    )

    companion object {
        private const val TAG = "EchoMessage"

        fun asrPartial(speakerId: Int, text: String, segmentId: Int) = EchoMessage(
            type = EchoMessageType.ASR_PARTIAL,
            speakerId = speakerId,
            text = text,
            segmentId = segmentId,
        )

        fun asrConfirmed(speakerId: Int, text: String, segmentId: Int) = EchoMessage(
            type = EchoMessageType.ASR_CONFIRMED,
            speakerId = speakerId,
            text = text,
            segmentId = segmentId,
        )

        fun translationStream(speakerId: Int, token: String, segmentId: Int) = EchoMessage(
            type = EchoMessageType.TRANSLATION_STREAM,
            speakerId = speakerId,
            text = token,
            segmentId = segmentId,
        )

        fun translationDone(speakerId: Int, text: String, segmentId: Int) = EchoMessage(
            type = EchoMessageType.TRANSLATION_DONE,
            speakerId = speakerId,
            text = text,
            segmentId = segmentId,
        )

        fun error(code: Int, detail: String) = EchoMessage(
            type = EchoMessageType.ERROR,
            errorCode = code,
            detail = detail,
        )

        fun engineReady(status: String) = EchoMessage(
            type = EchoMessageType.ENGINE_READY,
            text = status,
        )

        fun thermalState(mode: Int, detail: String) = EchoMessage(
            type = EchoMessageType.THERMAL_STATE,
            errorCode = mode,
            detail = detail,
        )
    }
}
