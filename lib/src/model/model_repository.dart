/// Local model provisioning service (air-gapped, no network).
///
/// Resolves the application sandbox directory, imports GGUF model files from
/// on-device storage into a private `models/` folder, validates them, reports
/// per-model status, and hands resolved paths to the engine.
///
/// This service performs ZERO network I/O — it only copies files that already
/// exist on the device, satisfying QwenEcho's air-gapped policy (Req 13.2).
library;

import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

import 'package:path_provider/path_provider.dart';

import 'model_catalog.dart';

/// GGUF magic bytes, little-endian 0x46475547 → ASCII "GGUF".
const List<int> _kGgufMagic = <int>[0x47, 0x47, 0x55, 0x46];

/// Status of a single model on disk.
class ModelStatus {
  /// The model this status describes.
  final ModelSpec spec;

  /// Absolute path where the model is (or would be) stored in the sandbox.
  final String path;

  /// Whether the file exists on disk.
  final bool present;

  /// On-disk size in bytes (0 if absent).
  final int sizeBytes;

  /// Whether the file passed GGUF magic-byte validation.
  final bool validGguf;

  const ModelStatus({
    required this.spec,
    required this.path,
    required this.present,
    required this.sizeBytes,
    required this.validGguf,
  });

  /// True when the model is present and passes basic validation.
  bool get isReady => present && validGguf;

  /// True when the file exceeds its allowed on-disk ceiling.
  bool get exceedsSizeLimit => present && sizeBytes > spec.maxSizeBytes;
}

/// Progress event emitted while importing (copying) a model file.
class ModelImportProgress {
  /// Bytes copied so far.
  final int copied;

  /// Total bytes to copy (0 if unknown).
  final int total;

  const ModelImportProgress(this.copied, this.total);

  /// Copy completion fraction in [0.0, 1.0]; 0 when total is unknown.
  double get fraction => total > 0 ? (copied / total).clamp(0.0, 1.0) : 0.0;
}

/// Thrown when importing a model fails validation or I/O.
class ModelImportException implements Exception {
  final String message;
  const ModelImportException(this.message);
  @override
  String toString() => 'ModelImportException: $message';
}

/// Manages local storage and provisioning of the required GGUF models.
class ModelRepository {
  Directory? _cachedDir;

  /// Resolve (and lazily create) the sandbox `models/` directory.
  ///
  /// Uses the application support directory, which lives inside the app
  /// sandbox on both Android and iOS and is not user-visible or shared.
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

  /// Absolute destination path for [spec] inside the sandbox.
  Future<String> pathFor(ModelSpec spec) async {
    final dir = await modelsDir();
    return '${dir.path}/${spec.fileName}';
  }

  /// Compute the status of a single model.
  ///
  /// Checks the user-imported sandbox copy first; if not found, falls back
  /// to a model bundled in the app's Flutter assets (read-only). This allows
  /// shipping pre-bundled models for development/debugging while still letting
  /// users override them with their own imports.
  Future<ModelStatus> statusFor(ModelSpec spec) async {
    // 1. User-imported copy in the writable sandbox.
    final sandboxPath = await pathFor(spec);
    final sandboxFile = File(sandboxPath);
    if (await sandboxFile.exists()) {
      final size = await sandboxFile.length();
      final valid = await _hasGgufMagic(sandboxFile);
      return ModelStatus(
        spec: spec,
        path: sandboxPath,
        present: true,
        sizeBytes: size,
        validGguf: valid,
      );
    }

    // 2. Pre-bundled copy in Flutter assets (read-only, mmap-able).
    final bundledPath = _bundledPathFor(spec);
    if (bundledPath != null) {
      final bundledFile = File(bundledPath);
      if (await bundledFile.exists()) {
        final size = await bundledFile.length();
        final valid = await _hasGgufMagic(bundledFile);
        return ModelStatus(
          spec: spec,
          path: bundledPath,
          present: true,
          sizeBytes: size,
          validGguf: valid,
        );
      }
    }

    // 3. Not found in either location.
    return ModelStatus(
      spec: spec,
      path: sandboxPath,
      present: false,
      sizeBytes: 0,
      validGguf: false,
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

  /// Import a model by copying [sourcePath] into the sandbox as [spec].
  ///
  /// Emits [ModelImportProgress] events while copying and completes when the
  /// file is fully written and validated. The copy is streamed so large files
  /// (multi-GB LLM weights) do not need to fit in memory.
  ///
  /// Throws [ModelImportException] if the source is missing, the header is not
  /// a valid GGUF magic, or the file exceeds the allowed size ceiling.
  Stream<ModelImportProgress> importModel(
    ModelSpec spec,
    String sourcePath,
  ) async* {
    final source = File(sourcePath);
    if (!await source.exists()) {
      throw const ModelImportException('Source file does not exist.');
    }

    final total = await source.length();
    if (total == 0) {
      throw const ModelImportException('Source file is empty.');
    }
    if (total > spec.maxSizeBytes) {
      throw ModelImportException(
        'File is ${_fmtBytes(total)}, exceeding the '
        '${_fmtBytes(spec.maxSizeBytes)} limit for ${spec.displayName}.',
      );
    }

    // Validate GGUF magic before committing to a large copy.
    if (!await _hasGgufMagic(source)) {
      throw const ModelImportException(
        'Not a valid GGUF file (missing "GGUF" magic bytes).',
      );
    }

    final destPath = await pathFor(spec);
    final tmpPath = '$destPath.part';
    final tmp = File(tmpPath);
    if (await tmp.exists()) {
      await tmp.delete();
    }

    final sink = tmp.openWrite();
    var copied = 0;
    try {
      await for (final chunk in source.openRead()) {
        sink.add(chunk);
        copied += chunk.length;
        yield ModelImportProgress(copied, total);
      }
      await sink.flush();
      await sink.close();
    } catch (e) {
      await sink.close();
      if (await tmp.exists()) await tmp.delete();
      throw ModelImportException('Copy failed: $e');
    }

    // Atomically move the completed temp file into place.
    final dest = File(destPath);
    if (await dest.exists()) {
      await dest.delete();
    }
    await tmp.rename(destPath);

    yield ModelImportProgress(total, total);
  }

  /// Delete a model file from the sandbox. Safe to call when absent.
  Future<void> deleteModel(ModelSpec spec) async {
    final file = File(await pathFor(spec));
    if (await file.exists()) {
      await file.delete();
    }
  }

  // ---------------------------------------------------------------------------
  // Private helpers
  // ---------------------------------------------------------------------------

  /// Filesystem path to the Flutter assets directory inside the app bundle
  /// (read-only). Returns null on platforms where this path is unavailable.
  String? get _flutterAssetsDir {
    if (!Platform.isIOS && !Platform.isMacOS) return null;
    // Platform.executable on iOS = .../Runner.app/Runner
    // Flutter assets = .../Runner.app/Frameworks/App.framework/flutter_assets
    final bundleDir = File(Platform.executable).parent;
    return '${bundleDir.path}/Frameworks/App.framework/flutter_assets';
  }

  /// Path to a model bundled in Flutter assets, or null if not applicable.
  String? _bundledPathFor(ModelSpec spec) {
    final dir = _flutterAssetsDir;
    if (dir == null) return null;
    return '$dir/models/${spec.fileName}';
  }

  Future<bool> _hasGgufMagic(File file) async {
    RandomAccessFile? raf;
    try {
      raf = await file.open();
      final Uint8List head = await raf.read(4);
      if (head.length < 4) return false;
      for (var i = 0; i < 4; i++) {
        if (head[i] != _kGgufMagic[i]) return false;
      }
      return true;
    } catch (_) {
      return false;
    } finally {
      await raf?.close();
    }
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
