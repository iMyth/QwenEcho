import Foundation

/// Message types flowing through the interpretation pipeline.
/// Serialized as dictionaries and sent to Flutter via EventChannel.
enum EchoMessageType: Int {
    case asrPartial = 0
    case asrConfirmed = 1
    case translationStream = 2
    case translationDone = 3
    case error = 4
    case thermalState = 5
    case latencyWarning = 6
    case engineReady = 7
}

/// A typed message that can be sent to Flutter.
struct EchoMessage {
    let type: EchoMessageType
    let speakerId: Int
    let text: String
    let segmentId: Int
    let timestampMs: Int64
    let errorCode: Int
    let detail: String

    /// Convert to a dictionary for FlutterEventSink.
    func toMap() -> [String: Any] {
        return [
            "type": type.rawValue,
            "speakerId": speakerId,
            "text": text,
            "segmentId": segmentId,
            "timestampMs": timestampMs,
            "errorCode": errorCode,
            "detail": detail,
        ]
    }

    // Convenience constructors
    static func asrPartial(speakerId: Int, text: String, segmentId: Int) -> EchoMessage {
        EchoMessage(type: .asrPartial, speakerId: speakerId, text: text,
                    segmentId: segmentId, timestampMs: Int64(Date().timeIntervalSince1970 * 1000),
                    errorCode: 0, detail: "")
    }

    static func asrConfirmed(speakerId: Int, text: String, segmentId: Int) -> EchoMessage {
        EchoMessage(type: .asrConfirmed, speakerId: speakerId, text: text,
                    segmentId: segmentId, timestampMs: Int64(Date().timeIntervalSince1970 * 1000),
                    errorCode: 0, detail: "")
    }

    static func translationStream(speakerId: Int, token: String, segmentId: Int) -> EchoMessage {
        EchoMessage(type: .translationStream, speakerId: speakerId, text: token,
                    segmentId: segmentId, timestampMs: Int64(Date().timeIntervalSince1970 * 1000),
                    errorCode: 0, detail: "")
    }

    static func translationDone(speakerId: Int, text: String, segmentId: Int) -> EchoMessage {
        EchoMessage(type: .translationDone, speakerId: speakerId, text: text,
                    segmentId: segmentId, timestampMs: Int64(Date().timeIntervalSince1970 * 1000),
                    errorCode: 0, detail: "")
    }

    static func error(code: Int, detail: String) -> EchoMessage {
        EchoMessage(type: .error, speakerId: 0, text: "", segmentId: 0,
                    timestampMs: Int64(Date().timeIntervalSince1970 * 1000),
                    errorCode: code, detail: detail)
    }

    static func engineReady(status: String) -> EchoMessage {
        EchoMessage(type: .engineReady, speakerId: 0, text: status, segmentId: 0,
                    timestampMs: Int64(Date().timeIntervalSince1970 * 1000),
                    errorCode: 0, detail: "")
    }

    static func thermalState(mode: Int, detail: String) -> EchoMessage {
        EchoMessage(type: .thermalState, speakerId: 0, text: "", segmentId: 0,
                    timestampMs: Int64(Date().timeIntervalSince1970 * 1000),
                    errorCode: mode, detail: detail)
    }
}
