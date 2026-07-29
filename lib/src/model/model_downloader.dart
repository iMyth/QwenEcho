import 'dart:async';
import 'dart:io';
import 'package:archive/archive.dart';
import 'package:http/http.dart' as http;
import 'package:qwen_echo/src/model/model_catalog.dart';
import 'package:qwen_echo/src/model/model_storage.dart';

/// Download progress information.
class DownloadProgress {
  /// Model being downloaded.
  final ModelSpec model;

  /// Bytes downloaded so far.
  final int downloadedBytes;

  /// Total bytes to download (if known).
  final int? totalBytes;

  /// Download speed in bytes per second.
  final double bytesPerSecond;

  /// Whether the download is complete.
  final bool isComplete;

  /// Error message if download failed.
  final String? error;

  const DownloadProgress({
    required this.model,
    required this.downloadedBytes,
    this.totalBytes,
    this.bytesPerSecond = 0,
    this.isComplete = false,
    this.error,
  });

  /// Progress as a percentage (0.0 to 1.0).
  double get progress {
    if (totalBytes == null || totalBytes == 0) return 0;
    return (downloadedBytes / totalBytes!).clamp(0.0, 1.0);
  }

  /// Human-readable progress string.
  String get progressText {
    final downloaded = _formatBytes(downloadedBytes);
    if (totalBytes != null) {
      final total = _formatBytes(totalBytes!);
      return '$downloaded / $total (${(progress * 100).toStringAsFixed(1)}%)';
    }
    return downloaded;
  }

  String _formatBytes(int bytes) {
    if (bytes < 1024) return '$bytes B';
    if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(1)} KB';
    if (bytes < 1024 * 1024 * 1024) {
      return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MB';
    }
    return '${(bytes / (1024 * 1024 * 1024)).toStringAsFixed(1)} GB';
  }
}

/// Download status for a model.
enum DownloadStatus {
  notStarted,
  downloading,
  paused,
  completed,
  failed,
}

/// Manages model downloads with progress tracking.
///
/// Features:
/// - Progress streaming
/// - Pause/resume support (future enhancement)
/// - MD5 checksum validation (future enhancement)
/// - Retry logic
class ModelDownloader {
  ModelDownloader._();
  static final ModelDownloader instance = ModelDownloader._();

  final ModelStorage _storage = ModelStorage.instance;

  // Track active downloads
  final Map<String, DownloadStatus> _downloadStatus = {};
  final Map<String, StreamController<DownloadProgress>> _progressControllers = {};

  // LLM: GitHub Releases
  static const String _llmUrl =
      'https://github.com/iMyth/QwenEcho/releases/download/v0.1.0/Qwen3.5-0.8B-Q4_K_M.gguf';

  // ASR: SenseVoice-Small model package (tar.bz2 archive)
  static const String _asrUrl =
      'https://github.com/iMyth/QwenEcho/releases/download/v0.1.0/SenseVoiceSmall-onnx.tar.bz2';

  /// Get the download URL for a model.
  String _getDownloadUrl(ModelSpec spec) {
    if (spec.kind == ModelKind.asr) {
      return _asrUrl;
    } else if (spec.kind == ModelKind.llm) {
      return _llmUrl;
    }
    throw UnsupportedError('Unknown model kind: ${spec.kind}');
  }

  /// Check download status for a model.
  DownloadStatus getStatus(ModelSpec spec) {
    return _downloadStatus[spec.dirName] ?? DownloadStatus.notStarted;
  }

  /// Get progress stream for a model.
  Stream<DownloadProgress> getProgressStream(ModelSpec spec) {
    final key = spec.dirName;
    if (!_progressControllers.containsKey(key)) {
      _progressControllers[key] = StreamController<DownloadProgress>.broadcast();
    }
    return _progressControllers[key]!.stream;
  }

  /// Download a model.
  ///
  /// Returns a stream of progress updates.
  Stream<DownloadProgress> downloadModel(ModelSpec spec) async* {
    final key = spec.dirName;

    // Check if already downloading
    if (_downloadStatus[key] == DownloadStatus.downloading) {
      yield DownloadProgress(
        model: spec,
        downloadedBytes: 0,
        error: 'Download already in progress',
      );
      return;
    }

    // Check if already downloaded
    if (await _storage.isModelDownloaded(spec)) {
      yield DownloadProgress(
        model: spec,
        downloadedBytes: 0,
        totalBytes: 0,
        isComplete: true,
      );
      return;
    }

    _downloadStatus[key] = DownloadStatus.downloading;

    try {
      // Ensure models directory exists
      await _storage.ensureModelsDirExists();

      final url = _getDownloadUrl(spec);
      final modelPath = await _storage.getModelPath(spec);

      // Create temporary download file
      final tempFile = File('$modelPath.tmp');
      await tempFile.create(recursive: true);

      // Start download with progress tracking
      final client = http.Client();
      final request = http.Request('GET', Uri.parse(url));
      final response = await client.send(request);

      if (response.statusCode != 200) {
        throw HttpException('Download failed: HTTP ${response.statusCode}');
      }

      final totalBytes = response.contentLength;
      int downloadedBytes = 0;
      final startTime = DateTime.now();

      final sink = tempFile.openWrite();

      await for (final chunk in response.stream) {
        if (_downloadStatus[key] != DownloadStatus.downloading) {
          // Download was cancelled
          await sink.close();
          await tempFile.delete();
          yield DownloadProgress(
            model: spec,
            downloadedBytes: downloadedBytes,
            totalBytes: totalBytes,
            error: 'Download cancelled',
          );
          return;
        }

        sink.add(chunk);
        downloadedBytes += chunk.length;

        // Calculate speed
        final elapsed = DateTime.now().difference(startTime).inMilliseconds / 1000.0;
        final speed = elapsed > 0 ? downloadedBytes / elapsed : 0.0;

        yield DownloadProgress(
          model: spec,
          downloadedBytes: downloadedBytes,
          totalBytes: totalBytes,
          bytesPerSecond: speed,
        );

        // Also push to progress controller
        _progressControllers[key]?.add(DownloadProgress(
          model: spec,
          downloadedBytes: downloadedBytes,
          totalBytes: totalBytes,
          bytesPerSecond: speed,
        ));
      }

      await sink.close();

      // Move temp file to final location
      if (spec.kind == ModelKind.llm) {
        // LLM is a single file
        await tempFile.rename(modelPath);
      } else if (spec.kind == ModelKind.asr) {
        // ASR is a tar.bz2 archive - extract it
        await _extractAsrArchive(tempFile, modelPath);
        // Clean up the archive file after extraction
        if (await tempFile.exists()) {
          await tempFile.delete();
        }
      }

      _downloadStatus[key] = DownloadStatus.completed;

      yield DownloadProgress(
        model: spec,
        downloadedBytes: downloadedBytes,
        totalBytes: totalBytes,
        bytesPerSecond: 0,
        isComplete: true,
      );

      _progressControllers[key]?.add(DownloadProgress(
        model: spec,
        downloadedBytes: downloadedBytes,
        totalBytes: totalBytes,
        bytesPerSecond: 0,
        isComplete: true,
      ));

      client.close();
    } catch (e) {
      _downloadStatus[key] = DownloadStatus.failed;

      yield DownloadProgress(
        model: spec,
        downloadedBytes: 0,
        error: e.toString(),
      );

      _progressControllers[key]?.add(DownloadProgress(
        model: spec,
        downloadedBytes: 0,
        error: e.toString(),
      ));
    }
  }

  /// Cancel a download.
  void cancelDownload(ModelSpec spec) {
    final key = spec.dirName;
    if (_downloadStatus[key] == DownloadStatus.downloading) {
      _downloadStatus[key] = DownloadStatus.failed;
    }
  }

  /// Download all required models sequentially.
  Stream<DownloadProgress> downloadAllModels() async* {
    for (final spec in kRequiredModels) {
      await for (final progress in downloadModel(spec)) {
        yield progress;
        if (progress.isComplete || progress.error != null) {
          break;
        }
      }
    }
  }

  /// Extract ASR model from tar.bz2 archive.
  ///
  /// The archive contains a directory like `sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/`
  /// with `model.int8.onnx`, `tokens.txt`, etc. We extract it and rename to
  /// the expected directory name (`SenseVoiceSmall-onnx`).
  Future<void> _extractAsrArchive(File archiveFile, String targetPath) async {
    // Read the archive file
    final archiveBytes = await archiveFile.readAsBytes();
    final archive = BZip2Decoder().decodeBytes(archiveBytes);
    final tarArchive = TarDecoder().decodeBytes(archive);

    // Find the root directory name inside the archive
    // (e.g., "sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/")
    String? archiveRootDir;
    for (final file in tarArchive) {
      if (file.isFile) {
        final parts = file.name.split('/');
        if (parts.length > 1) {
          archiveRootDir = parts.first;
          break;
        }
      }
    }

    if (archiveRootDir == null) {
      throw Exception('Invalid ASR archive: no root directory found');
    }

    // Create target directory
    final targetDir = Directory(targetPath);
    if (await targetDir.exists()) {
      await targetDir.delete(recursive: true);
    }
    await targetDir.create(recursive: true);

    // Extract files, stripping the root directory prefix
    for (final file in tarArchive) {
      // Strip the archive root directory from the path
      String relativePath = file.name;
      if (relativePath.startsWith('$archiveRootDir/')) {
        relativePath = relativePath.substring(archiveRootDir.length + 1);
      } else if (relativePath == archiveRootDir || relativePath == '$archiveRootDir/') {
        continue; // Skip the root directory entry itself
      }

      if (relativePath.isEmpty) continue;

      final extractedPath = '${targetDir.path}/$relativePath';

      if (file.isFile) {
        // Ensure parent directory exists
        final parentDir = Directory(File(extractedPath).parent.path);
        if (!await parentDir.exists()) {
          await parentDir.create(recursive: true);
        }
        await File(extractedPath).writeAsBytes(file.content as List<int>);
      } else if (file.isDirectory) {
        await Directory(extractedPath).create(recursive: true);
      }
    }
  }
}
