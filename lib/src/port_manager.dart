/// Port Manager — registers a Dart Native Port with the engine and
/// deserializes incoming messages into typed [EchoMessage] objects.
library;

import 'dart:async';
import 'dart:ffi';
import 'dart:isolate';

import 'messages.dart';
import 'native_bridge.dart';

/// Manages the Native Port connection between the Flutter UI Shell and the
/// QwenEcho native engine.
///
/// Creates a [ReceivePort], registers it with the engine via
/// [NativeBridge.registerPort], and transforms raw incoming lists into typed
/// [EchoMessage] objects exposed as a broadcast [Stream].
class PortManager {
  final NativeBridge _bridge;

  ReceivePort? _receivePort;
  StreamSubscription<dynamic>? _subscription;

  final StreamController<EchoMessage> _messageController =
      StreamController<EchoMessage>.broadcast();

  /// Whether the port is currently registered and listening.
  bool get isListening => _receivePort != null;

  /// Stream of typed messages from the native engine.
  ///
  /// Multiple listeners can subscribe (broadcast stream).
  Stream<EchoMessage> get messages => _messageController.stream;

  /// Create a [PortManager] bound to the given [NativeBridge].
  PortManager(this._bridge);

  /// Register a new Native Port with the engine and begin listening.
  ///
  /// If a port is already registered, it will be closed and replaced.
  /// Throws [EchoEngineException] if registration fails.
  void register() {
    // Close any existing port first.
    _closePort();

    _receivePort = ReceivePort('QwenEchoPort');
    _bridge.registerPort(_receivePort!.sendPort.nativePort);

    _subscription = _receivePort!.listen(_handleRawMessage);
  }

  /// Close the current port and stop listening.
  ///
  /// Messages in-flight may be lost after this call.
  void unregister() {
    _closePort();
  }

  /// Dispose of all resources. Call when the engine is being torn down.
  void dispose() {
    _closePort();
    _messageController.close();
  }

  // ---------------------------------------------------------------------------
  // Private
  // ---------------------------------------------------------------------------

  void _closePort() {
    _subscription?.cancel();
    _subscription = null;
    _receivePort?.close();
    _receivePort = null;
  }

  void _handleRawMessage(dynamic raw) {
    if (raw is! List) return;

    final message = EchoMessage.fromRawList(raw);
    if (message != null) {
      _messageController.add(message);
    }
  }
}
