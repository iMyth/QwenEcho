import 'dart:io';
import 'package:path_provider/path_provider.dart';
import 'package:qwen_echo/src/model/model_catalog.dart';

/// Manages model storage on disk.
///
/// Handles:
/// - Finding the correct storage path per platform
/// - Checking if models are downloaded
/// - Deleting models to free space
/// - Getting disk usage stats
class ModelStorage {
  ModelStorage._();
  static final ModelStorage instance = ModelStorage._();

  /// Get the base directory for model storage.
  ///
  /// Uses the same path as ModelRepository to ensure consistency:
  /// - All platforms: Application Support/models/
  Future<Directory> _getModelsBaseDir() async {
    final base = await getApplicationSupportDirectory();
    return Directory('${base.path}/models');
  }

  /// Get the full path for a specific model.
  Future<String> getModelPath(ModelSpec spec) async {
    final baseDir = await _getModelsBaseDir();
    return '${baseDir.path}/${spec.dirName}';
  }

  /// Check if a model is downloaded and complete.
  ///
  /// Returns the model path if found, null otherwise.
  Future<String?> findModel(ModelSpec spec) async {
    final modelPath = await getModelPath(spec);

    if (spec.kind == ModelKind.llm) {
      // LLM is a single GGUF file
      final file = File(modelPath);
      return await file.exists() ? modelPath : null;
    } else if (spec.kind == ModelKind.asr) {
      // ASR is a directory with key files
      final modelDir = Directory(modelPath);
      if (!await modelDir.exists()) {
        return null;
      }

      // Check for key ASR files
      final onnxFile = File('$modelPath/model.int8.onnx');
      final tokensFile = File('$modelPath/tokens.txt');

      if (await onnxFile.exists() && await tokensFile.exists()) {
        return modelPath;
      }
      return null;
    }

    return null;
  }

  /// Check if a model is downloaded.
  Future<bool> isModelDownloaded(ModelSpec spec) async {
    return await findModel(spec) != null;
  }

  /// Delete a model to free disk space.
  Future<void> deleteModel(ModelSpec spec) async {
    final modelPath = await getModelPath(spec);
    final modelDir = Directory(modelPath);

    if (await modelDir.exists()) {
      await modelDir.delete(recursive: true);
    }
  }

  /// Get total disk space used by all models.
  Future<int> getTotalModelSize() async {
    final baseDir = await _getModelsBaseDir();

    if (!await baseDir.exists()) {
      return 0;
    }

    int total = 0;
    await for (final entity
        in baseDir.list(recursive: true, followLinks: false)) {
      if (entity is File) {
        total += await entity.length();
      }
    }

    return total;
  }

  /// Get available disk space in bytes.
  ///
  /// Returns a conservative estimate. For accurate disk space checking,
  /// a platform-specific implementation or plugin (e.g., `free_disk_space`)
  /// should be used.
  ///
  /// This implementation checks if the storage directory is writable and
  /// returns a conservative estimate suitable for preflight checks.
  Future<int> getAvailableSpace() async {
    final baseDir = await _getModelsBaseDir();
    final parent = baseDir.parent;

    try {
      // Ensure parent directory exists
      if (!await parent.exists()) {
        await parent.create(recursive: true);
      }

      // Try to write a small test file to verify writability
      final testFile = File('${parent.path}/.disk_space_test');
      await testFile.writeAsString('test');
      await testFile.delete();

      // Return a conservative estimate.
      // In production, this should be replaced with a platform-specific
      // implementation that queries actual free space.
      // For now, assume at least 2GB is available if the directory is writable.
      // Users with less space will encounter download failures, which are
      // handled gracefully by the downloader.
      return 2 * 1024 * 1024 * 1024; // 2 GB conservative estimate
    } catch (e) {
      // Directory not writable or other error
      return 0;
    }
  }

  /// Ensure the models directory exists.
  Future<void> ensureModelsDirExists() async {
    final baseDir = await _getModelsBaseDir();
    if (!await baseDir.exists()) {
      await baseDir.create(recursive: true);
    }
  }
}
