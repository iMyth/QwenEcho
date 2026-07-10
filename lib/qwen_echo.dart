/// QwenEcho — On-device simultaneous interpretation engine.
///
/// This library exposes the MethodChannel-based engine API and typed message
/// definitions for communicating with the QwenEcho native Swift engine.
library qwen_echo;

export 'src/echo_engine.dart';
export 'src/messages.dart';
export 'src/model/model_catalog.dart';
export 'src/model/model_repository.dart';
export 'src/ui/model_config_screen.dart';
export 'src/ui/status_bar.dart';
export 'src/ui/warning_overlay.dart';
