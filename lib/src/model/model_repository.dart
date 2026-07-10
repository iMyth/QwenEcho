/// Local model provisioning service (air-gapped, no network).
///
/// Resolves the application sandbox directory, validates MLX model directories
/// (containing config.json + model.safetensors), reports per-model status,
/// and hands resolved paths to the engine.
///
/// This service performs ZERO network I/O — it only validates directories that
/// already exist on the device, satisfying QwenEcho's air-gapped policy.
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

  /// Whether the directory contains a valid MLX model (config.json present).
  final bool validMlx;

  const ModelStatus({
    required this.spec,
    required this.path,
    required this.present,
    required this.sizeBytes,
    required this.validMlx,
  });

  /// True when the model is present and passes validation.
  bool get isReady => present && validMlx;

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

/// Manages local storage and provisioning of the required MLX models.
///
/// MLX models are directories containing config.json, model.safetensors,
/// tokenizer.json, and other files. This class validates directory structure
/// and computes sizes.
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
  /// Checks the user-imported sandbox copy. An MLX model is valid when
  /// its directory contains a `config.json` file.
  Future<ModelStatus> statusFor(ModelSpec spec) async {
    final modelPath = await pathFor(spec);
    final modelDir = Directory(modelPath);

    if (await modelDir.exists()) {
      final size = await _directorySize(modelDir);
      final valid = await _hasMlxConfig(modelDir);
      return ModelStatus(
        spec: spec,
        path: modelPath,
        present: true,
        sizeBytes: size,
        validMlx: valid,
      );
    }

    // Check for pre-bundled copy in Flutter assets.
    final bundledPath = _bundledPathFor(spec);
    if (bundledPath != null) {
      final bundledDir = Directory(bundledPath);
      if (await bundledDir.exists()) {
        final size = await _directorySize(bundledDir);
        final valid = await _hasMlxConfig(bundledDir);
        return ModelStatus(
          spec: spec,
          path: bundledPath,
          present: true,
          sizeBytes: size,
          validMlx: valid,
        );
      }
    }

    return ModelStatus(
      spec: spec,
      path: modelPath,
      present: false,
      sizeBytes: 0,
      validMlx: false,
    );
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
  String? get _flutterAssetsDir {
    if (!Platform.isIOS && !Platform.isMacOS) return null;
    final bundleDir = File(Platform.executable).parent;
    return '${bundleDir.path}/Frameworks/App.framework/flutter_assets';
  }

  /// Path to a model bundled in Flutter assets, or null if not applicable.
  String? _bundledPathFor(ModelSpec spec) {
    final dir = _flutterAssetsDir;
    if (dir == null) return null;
    return '$dir/models/${spec.dirName}';
  }

  /// Check if a directory contains config.json (MLX model validation).
  Future<bool> _hasMlxConfig(Directory dir) async {
    try {
      final configFile = File('${dir.path}/config.json');
      return await configFile.exists();
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
