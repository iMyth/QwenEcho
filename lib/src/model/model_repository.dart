/// Local model provisioning service (air-gapped, no network).
///
/// Resolves the application sandbox directory, validates GGUF files and
/// sherpa-onnx model packages, reports per-model status, and hands resolved
/// paths to the engine.
///
/// This service performs ZERO network I/O — it only validates files and
/// directories that already exist on the device, satisfying QwenEcho's
/// air-gapped policy.
library;

import 'dart:io';

import 'package:path_provider/path_provider.dart';

import 'model_catalog.dart';

/// Status of a single model on disk.
class ModelStatus {
  /// The model this status describes.
  final ModelSpec spec;

  /// Absolute path where the model directory is (or would be) stored.
  final String path;

  /// Whether the model directory exists on disk.
  final bool present;

  /// On-disk size of the model directory in bytes (0 if absent).
  final int sizeBytes;

  /// Whether the model file or package passes format validation.
  final bool valid;

  const ModelStatus({
    required this.spec,
    required this.path,
    required this.present,
    required this.sizeBytes,
    required this.valid,
  });

  /// True when the model is present and passes validation.
  bool get isReady => present && valid;

  /// True when the directory exceeds its allowed on-disk ceiling.
  bool get exceedsSizeLimit => present && sizeBytes > spec.maxSizeBytes;
}

/// Thrown when validating a model fails.
class ModelImportException implements Exception {
  final String message;
  const ModelImportException(this.message);
  @override
  String toString() => 'ModelImportException: $message';
}

/// Manages local storage and provisioning of the required models.
///
/// Validates GGUF magic bytes for the LLM file and sherpa-onnx package
/// contents (`model.int8.onnx` + `tokens.txt`) for ASR.
class ModelRepository {
  Directory? _cachedDir;

  /// Resolve (and lazily create) the sandbox `models/` directory.
  Future<Directory> modelsDir() async {
    if (_cachedDir != null) return _cachedDir!;
    final base = await getApplicationSupportDirectory();
    final dir = Directory('${base.path}/models');
    if (!await dir.exists()) {
      await dir.create(recursive: true);
    }
    _cachedDir = dir;
    return dir;
  }

  /// Absolute directory path for [spec] inside the sandbox.
  Future<String> pathFor(ModelSpec spec) async {
    final dir = await modelsDir();
    return '${dir.path}/${spec.dirName}';
  }

  /// Compute the status of a single model.
  ///
  /// Checks in order:
  /// 1. App sandbox (`modelsDir()`)
  /// 2. External storage `/sdcard/QwenEcho/models/` (Android, for easy adb push)
  /// 3. Bundled Flutter assets (iOS/macOS only)
  Future<ModelStatus> statusFor(ModelSpec spec) async {
    final modelPath = await pathFor(spec);

    // Check app sandbox first
    final sandboxStatus = await _checkModel(spec, modelPath);
    if (sandboxStatus != null) return sandboxStatus;

    // On Android, also check app-specific external storage (no permission needed)
    // Path: /sdcard/Android/data/com.example.qwen_echo/files/models/
    if (Platform.isAndroid) {
      final extBase = await getExternalStorageDirectory();
      if (extBase != null) {
        final externalPath = '${extBase.path}/models/${spec.dirName}';
        final externalStatus = await _checkModel(spec, externalPath);
        if (externalStatus != null) return externalStatus;
      }
    }

    // Check bundled assets (iOS/macOS only)
    final bundledPath = _bundledPathFor(spec);
    if (bundledPath != null) {
      final bundledStatus = await _checkModel(spec, bundledPath);
      if (bundledStatus != null) return bundledStatus;
    }

    return ModelStatus(
      spec: spec,
      path: modelPath,
      present: false,
      sizeBytes: 0,
      valid: false,
    );
  }

  /// Check a single model path. Returns null if not present.
  Future<ModelStatus?> _checkModel(ModelSpec spec, String path) async {
    if (spec.kind == ModelKind.llm) {
      final file = File(path);
      if (await file.exists()) {
        final size = await file.length();
        final valid = await _isValidGguf(file);
        return ModelStatus(
          spec: spec, path: path, present: true,
          sizeBytes: size, valid: valid,
        );
      }
    } else {
      final dir = Directory(path);
      if (await dir.exists()) {
        final size = await _directorySize(dir);
        final valid = await _hasSherpaOnnxPackage(dir);
        return ModelStatus(
          spec: spec, path: path, present: true,
          sizeBytes: size, valid: valid,
        );
      }
    }
    return null;
  }

  /// Compute the status of all required models, in catalog order.
  Future<List<ModelStatus>> statusAll() async {
    return Future.wait(kRequiredModels.map(statusFor));
  }

  /// Whether every required model is present and valid.
  Future<bool> get isComplete async {
    final all = await statusAll();
    return all.every((s) => s.isReady);
  }

  /// Resolve engine model paths if — and only if — all models are ready.
  ///
  /// Returns null when any model is missing or invalid.
  Future<Map<ModelKind, String>?> resolvePathsIfComplete() async {
    final all = await statusAll();
    if (!all.every((s) => s.isReady)) return null;
    return {for (final s in all) s.spec.kind: s.path};
  }

  /// Delete a model directory from the sandbox. Safe to call when absent.
  Future<void> deleteModel(ModelSpec spec) async {
    final dir = Directory(await pathFor(spec));
    if (await dir.exists()) {
      await dir.delete(recursive: true);
    }
  }

  // ---------------------------------------------------------------------------
  // Private helpers
  // ---------------------------------------------------------------------------

  /// Filesystem path to the Flutter assets directory inside the app bundle.
  ///
  /// On iOS/macOS, assets are in `Frameworks/App.framework/flutter_assets`.
  /// On Android, assets are inside the APK and not directly accessible as files.
  /// For development, models should be downloaded via `scripts/setup_models.sh`
  /// or imported via the Files app.
  String? get _flutterAssetsDir {
    if (Platform.isIOS || Platform.isMacOS) {
      final bundleDir = File(Platform.executable).parent;
      return '${bundleDir.path}/Frameworks/App.framework/flutter_assets';
    }
    // Android: assets are in APK, not accessible as regular files.
    // Models must be downloaded or imported to the sandbox directory.
    return null;
  }

  /// Path to a model bundled in Flutter assets, or null if not applicable.
  String? _bundledPathFor(ModelSpec spec) {
    final dir = _flutterAssetsDir;
    if (dir == null) return null;
    return '$dir/models/${spec.dirName}';
  }

  /// Validate a GGUF file by checking its magic bytes (`GGUF`).
  Future<bool> _isValidGguf(File file) async {
    try {
      final bytes = await file.openRead(0, 4).first;
      return bytes.length >= 4 &&
          bytes[0] == 0x47 && // G
          bytes[1] == 0x47 && // G
          bytes[2] == 0x55 && // U
          bytes[3] == 0x46; // F
    } catch (_) {
      return false;
    }
  }

  /// Validate a SenseVoice-Small ONNX model package directory.
  ///
  /// sherpa-onnx SenseVoice releases use `model.int8.onnx`.
  Future<bool> _hasSherpaOnnxPackage(Directory dir) async {
    try {
      final onnx = File('${dir.path}/model.int8.onnx');
      final tokens = File('${dir.path}/tokens.txt');
      return await onnx.exists() && await tokens.exists();
    } catch (_) {
      return false;
    }
  }

  /// Recursively compute the total size of a directory in bytes.
  Future<int> _directorySize(Directory dir) async {
    int total = 0;
    try {
      await for (final entity in dir.list(recursive: true)) {
        if (entity is File) {
          total += await entity.length();
        }
      }
    } catch (_) {
      // Ignore errors, return what we have.
    }
    return total;
  }

  static String _fmtBytes(int bytes) {
    const units = ['B', 'KB', 'MB', 'GB'];
    double v = bytes.toDouble();
    var u = 0;
    while (v >= 1024 && u < units.length - 1) {
      v /= 1024;
      u++;
    }
    return '${v.toStringAsFixed(u == 0 ? 0 : 1)} ${units[u]}';
  }
}

/// Public byte formatter for UI reuse.
String formatBytes(int bytes) => ModelRepository._fmtBytes(bytes);
