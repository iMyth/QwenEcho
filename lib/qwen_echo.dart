/// QwenEcho — On-device simultaneous interpretation engine bindings.
///
/// This library exposes the Dart FFI bindings and typed message definitions
/// for communicating with the QwenEcho native C/C++ engine.
library qwen_echo;

export 'src/echo_engine.dart';
export 'src/messages.dart';
export 'src/native_bridge.dart' show EchoEngineException, EchoErrorCode;
export 'src/port_manager.dart';
export 'src/ui/status_bar.dart';
export 'src/ui/warning_overlay.dart';
