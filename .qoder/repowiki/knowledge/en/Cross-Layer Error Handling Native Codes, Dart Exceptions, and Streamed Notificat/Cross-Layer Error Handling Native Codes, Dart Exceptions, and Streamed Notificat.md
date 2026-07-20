---
kind: error_handling
name: 'Cross-Layer Error Handling: Native Codes, Dart Exceptions, and Streamed Notifications'
category: error_handling
scope:
    - '**'
source_files:
    - native/include/echo_types.h
    - lib/src/native_bridge.dart
    - lib/src/messages.dart
    - native/src/native_port.cpp
    - lib/src/model/model_repository.dart
---

QwenEcho implements a three-tier error handling strategy that spans the C/C++ native engine, the Dart FFI bridge, and the Flutter UI layer. Errors are represented differently at each boundary but follow consistent conventions for propagation and presentation.

## 1. Native Layer (C/C++) — Integer Return Codes + Asynchronous Messages

The native engine uses two complementary mechanisms:

- **Synchronous integer return codes** via `EchoErrorCode` (`native/include/echo_types.h`). All four C-linkage entry points (`InitQwenEchoEngine`, `StartEchoPipeline`, `StopEchoPipeline`, `RegisterEchoMessagePort`) return `int32_t`: zero means success, negative values encode specific failures such as `ECHO_ERR_MODEL_MISSING`, `ECHO_ERR_MEMORY`, `ECHO_ERR_THERMAL_CRITICAL`, etc. The enum is intentionally small and stable to keep the FFI surface minimal.

- **Asynchronous typed notifications** sent over the Dart Native Port via `MessageType` tags in `native/src/native_port.cpp`. Rather than blocking on every failure, the engine posts structured messages like `MSG_ERROR`, `MSG_MEMORY_WARNING`, `MSG_LATENCY_WARNING`, `MSG_THERMAL_STATE`, and `MSG_SAMPLE_DROP`. Each message is a fixed-layout array of `Dart_CObject`s with a leading type tag followed by payload fields (e.g., `[type, error_code, model_name, detail]` for errors). This lets background threads report transient conditions without stalling the caller.

Platform HALs use platform-native error types internally (AAudio result codes on Android, `NSError` on iOS) but translate them into the shared `EchoErrorCode` / `MessageType` vocabulary before crossing the FFI boundary.

## 2. FFI Bridge Layer (Dart) — Exception Throwing + Code Mirroring

The Dart FFI bridge (`lib/src/native_bridge.dart`) mirrors `EchoErrorCode` as an `abstract final class EchoErrorCode` with static int constants and a `describe(int)` switch expression producing human-readable English strings. Every public method (`initEngine`, `startPipeline`, `stopPipeline`, `registerPort`) wraps its native call in `_throwOnError(result)`, which throws a single `EchoEngineException(code, message)` when the returned code is non-zero. Memory allocated via `toNativeUtf8()` is always freed in a `finally` block so exceptions do not leak native memory.

This design keeps the Dart API idiomatic: callers catch `EchoEngineException` and can inspect `.code` for programmatic handling or use `.message` for display.

## 3. Application Layer (Dart) — Domain-Specific Exceptions + Typed Message Classes

Domain logic outside the FFI bridge defines its own exception types. `ModelImportException` (`lib/src/model/model_repository.dart`) represents GGUF model import failures (missing source, empty file, size limit exceeded, invalid magic bytes, copy I/O errors). It is thrown synchronously from `importModel` and caught by callers who present user-facing feedback.

For asynchronous pipeline events, `lib/src/messages.dart` defines a sealed hierarchy of `EchoMessage` subclasses (`ErrorMessage`, `ThermalStateMessage`, `MemoryWarningMessage`, `LatencyWarningMessage`, `SampleDropMessage`, plus ASR/translation/TTS progress messages). Each has a factory `_fromRaw(List<dynamic>)` that parses the raw Native Port array. The base `EchoMessage.fromRawList` dispatches on the leading type tag, returning `null` for unrecognized tags. Callers consume these objects from a shared stream and branch on the concrete subtype.

## 4. Architecture and Conventions

- **Single source of truth**: `EchoErrorCode` lives in the C header; the Dart mirror must stay in sync. Tests in `native/tests/test_echo_types.cpp` assert enum value stability.
- **Fail-fast on synchronous calls**: FFI methods throw immediately on non-zero returns rather than returning optional results.
- **Non-fatal telemetry via streams**: Resource pressure, latency SLA violations, thermal throttling, and audio drops are reported asynchronously through the message port so they never crash the pipeline.
- **No panics/recover**: The C++ code avoids `throw`/`std::terminate`; allocation failures use `new(std::nothrow)` and null checks. There is no `panic`/`recover` pattern anywhere.
- **Human-readable descriptions**: Both `EchoErrorCode.describe` and each `EchoMessage.toString` provide developer-friendly summaries suitable for logging or debug output.

## 5. Rules for Developers

1. When adding a new native failure, extend `EchoErrorCode` in `echo_types.h` and add the corresponding constant + description in Dart's `EchoErrorCode`.
2. For transient, non-fatal conditions (memory pressure, thermal state, latency), post a `native_port_post_*` message rather than returning an error code.
3. In Dart, wrap every FFI call through `_throwOnError` and let `EchoEngineException` propagate; do not swallow native errors silently.
4. Use domain-specific exceptions (`ModelImportException`, etc.) for Dart-only failures; reserve `EchoEngineException` for cross-boundary issues.
5. Keep `MessageType` tags and their Dart counterparts in lockstep — any new message needs a tag in both `echo_types.h` and `messages.dart`.