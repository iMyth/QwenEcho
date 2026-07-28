import Flutter
import UIKit

@main
@objc class AppDelegate: FlutterAppDelegate, FlutterImplicitEngineDelegate {
  override func application(
    _ application: UIApplication,
    didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?
  ) -> Bool {
    // EnginePlugin registration moved to didInitializeImplicitFlutterEngine
    // because window is nil here in scene-based lifecycle.
    return super.application(application, didFinishLaunchingWithOptions: launchOptions)
  }

  func didInitializeImplicitFlutterEngine(_ engineBridge: FlutterImplicitEngineBridge) {
    GeneratedPluginRegistrant.register(with: engineBridge.pluginRegistry)

    // Register the QwenEcho engine plugin (MethodChannel + EventChannel)
    if let registrar = engineBridge.pluginRegistry.registrar(forPlugin: "EnginePlugin") {
      EnginePlugin.register(with: registrar)
    }

    // Register the TTS player plugin (separate MethodChannel for speak/stop).
    if let registrar = engineBridge.pluginRegistry.registrar(forPlugin: "TtsPlayer") {
      TtsPlayer.register(with: registrar)
    }
  }
}
