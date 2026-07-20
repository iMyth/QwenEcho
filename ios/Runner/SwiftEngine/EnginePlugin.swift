import Flutter
import UIKit
import os

/// Flutter plugin bridge for the QwenEcho engine.
///
/// Exposes MethodChannel for lifecycle commands (init, start, stop)
/// and EventChannel for streaming messages (ASR, translation, errors).
final class EnginePlugin: NSObject, FlutterPlugin, FlutterStreamHandler {

    private let pipeline: PipelineController
    private let messageStream = MessageStream()

    // Keep engine alive
    private static var instance: EnginePlugin?

    override init() {
        pipeline = PipelineController(messages: messageStream)
        super.init()
    }

    // MARK: - FlutterPlugin

    static func register(with registrar: FlutterPluginRegistrar) {
        let instance = EnginePlugin()
        Self.instance = instance

        // MethodChannel for lifecycle commands
        let methodChannel = FlutterMethodChannel(
            name: "qwen_echo_engine",
            binaryMessenger: registrar.messenger()
        )
        registrar.addMethodCallDelegate(instance, channel: methodChannel)

        // EventChannel for streaming messages
        let eventChannel = FlutterEventChannel(
            name: "qwen_echo_events",
            binaryMessenger: registrar.messenger()
        )
        eventChannel.setStreamHandler(instance)
    }

    func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        switch call.method {
        case "initialize":
            handleInitialize(call, result: result)

        case "start":
            handleStart(call, result: result)

        case "stop":
            pipeline.stop()
            result(nil)

        case "test_inject":
            guard let args = call.arguments as? [String: Any],
                  let text = args["text"] as? String else {
                result(FlutterError(code: "invalid_args", message: "Missing text", details: nil))
                return
            }
            let speakerId = args["speakerId"] as? Int ?? 0
            pipeline.injectTestText(text, speakerId: speakerId)
            result(["success": true])

        case "getPlatformVersion":
            result("iOS " + UIDevice.current.systemVersion)

        default:
            result(FlutterMethodNotImplemented)
        }
    }

    // MARK: - FlutterStreamHandler

    func onListen(withArguments arguments: Any?, eventSink events: @escaping FlutterEventSink) -> FlutterError? {
        messageStream.setSink(events)
        return nil
    }

    func onCancel(withArguments arguments: Any?) -> FlutterError? {
        messageStream.clearSink()
        return nil
    }

    // MARK: - Method Handlers

    private func handleInitialize(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        guard let args = call.arguments as? [String: String],
              let asrPath = args["asrPath"],
              let llmPath = args["llmPath"] else {
            result(FlutterError(code: "invalid_args", message: "Missing asrPath or llmPath", details: nil))
            return
        }

        // LLM inference runs in Dart via llamadart; only ASR is native.
        pipeline.initialize(asrPath: asrPath)
        os_log("[EnginePlugin] LLM path passed to Dart side: %{public}@", llmPath)

        result(["success": true])
    }

    private func handleStart(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        guard let args = call.arguments as? [String: String],
              let srcLang = args["srcLang"],
              let tgtLang = args["tgtLang"] else {
            result(FlutterError(code: "invalid_args", message: "Missing language pair", details: nil))
            return
        }

        // `pipeline.start` is async because it must request microphone
        // permission before touching AVAudioEngine. The result callback can
        // be invoked asynchronously — Flutter's MethodChannel supports that.
        Task {
            if let errorMessage = await pipeline.start(srcLang: srcLang, tgtLang: tgtLang) {
                result(FlutterError(code: "start_failed",
                                    message: errorMessage,
                                    details: nil))
            } else {
                result(["success": true])
            }
        }
    }
}
