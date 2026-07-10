import Flutter
import UIKit
import Speech

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
              let llmPath = args["llmPath"] else {
            result(FlutterError(code: "invalid_args", message: "Missing llmPath", details: nil))
            return
        }

        pipeline.initialize(llmPath: llmPath)

        // Request speech recognition authorization
        Task {
            let status = await AsrStage.requestAuthorization()
            if status != .authorized {
                DispatchQueue.main.async {
                    self.messageStream.post(.error(
                        code: -4,
                        detail: "Speech recognition not authorized: \(status.rawValue)"
                    ))
                }
            }
        }

        result(["success": true])
    }

    private func handleStart(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        guard let args = call.arguments as? [String: String],
              let srcLang = args["srcLang"],
              let tgtLang = args["tgtLang"] else {
            result(FlutterError(code: "invalid_args", message: "Missing language pair", details: nil))
            return
        }

        pipeline.start(srcLang: srcLang, tgtLang: tgtLang)
        result(["success": true])
    }
}
