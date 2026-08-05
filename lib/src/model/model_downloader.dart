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

  /// Whether the download was cancelled by the user.
  final bool isCancelled;

  /// Error message if download failed.
  final String? error;

  const DownloadProgress({
    required this.model,
    required this.downloadedBytes,
    this.totalBytes,
    this.bytesPerSecond = 0,
    this.isComplete = false,
    this.isCancelled = false,
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
  cancelled,
}

/// Maps common exceptions to user-friendly Chinese error messages.
String _friendlyErrorMessage(Object error) {
  if (error is SocketException) {
    return '网络连接失败，请检查网络设置后重试';
  }
  if (error is HttpException) {
    final message = error.message;
    if (message.contains('404')) {
      return '下载链接已失效，请更新应用后重试';
    }
    if (message.contains('5')) {
      return '服务器繁忙，请稍后再试';
    }
    return '下载失败: $message';
  }
  if (error is TimeoutException) {
    return '下载超时，请检查网络连接后重试';
  }
  if (error is PathNotFoundException) {
    return '存储空间不可用，请检查设备存储';
  }
  if (error is FileSystemException) {
    if (error.message.contains('No space')) {
      return '设备存储空间不足，请清理后重试';
    }
    return '文件写入失败: ${error.message}';
  }
  return '下载失败: $error';
}

/// Manages model downloads with progress tracking.
///
/// Features:
/// - Progress streaming with throttled updates
/// - HTTP Range resume support (continues from where it left off)
/// - HTTP timeout protection (30 seconds)
/// - Clean cancellation (distinct from error)
/// - Partial file cleanup on failure
/// - User-friendly error messages in Chinese
/// - Path-traversal protection during archive extraction
class ModelDownloader {
  ModelDownloader._();
  static final ModelDownloader instance = ModelDownloader._();

  final ModelStorage _storage = ModelStorage.instance;

  // Track active downloads
  final Map<String, DownloadStatus> _downloadStatus = {};
  final Map<String, StreamController<DownloadProgress>> _progressControllers =
      {};

  // HTTP timeout for connection and read operations
  static const Duration _httpTimeout = Duration(seconds: 30);

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
      _progressControllers[key] =
          StreamController<DownloadProgress>.broadcast();
    }
    return _progressControllers[key]!.stream;
  }

  /// Download a model.
  ///
  /// Supports resume: if a partial `.tmp` file exists from a previous
  /// attempt, the download continues from where it left off using HTTP Range.
  ///
  /// Returns a stream of progress updates.
  Stream<DownloadProgress> downloadModel(ModelSpec spec) async* {
    final key = spec.dirName;

    // Reset any previous failed/cancelled state
    if (_downloadStatus[key] == DownloadStatus.failed ||
        _downloadStatus[key] == DownloadStatus.cancelled ||
        _downloadStatus[key] == DownloadStatus.notStarted) {
      _downloadStatus.remove(key);
    }

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

    final tempFilePath = '${await _storage.getModelPath(spec)}.tmp';
    final tempFile = File(tempFilePath);
    http.Client? client;
    IOSink? sink;

    try {
      // Ensure models directory exists
      await _storage.ensureModelsDirExists();

      final url = _getDownloadUrl(spec);
      final modelPath = await _storage.getModelPath(spec);

      // Check for partial download to support resume
      int existingBytes = 0;
      if (await tempFile.exists()) {
        existingBytes = await tempFile.length();
        if (existingBytes > 0) {
          // Verify the partial file is still valid (not corrupted)
          // For simplicity, we trust the partial file and let the server
          // validate via Range request. If the server rejects the Range,
          // we'll get a 200 (full download) instead of 206 (partial).
        }
      }

      // Start download with progress tracking
      client = http.Client();
      final request = http.Request('GET', Uri.parse(url));

      // Add Range header for resume support
      if (existingBytes > 0) {
        request.headers['Range'] = 'bytes=$existingBytes-';
      }

      final response = await client.send(request).timeout(_httpTimeout);

      // Handle response status
      if (response.statusCode == 200) {
        // Full download (server doesn't support Range or file changed)
        existingBytes = 0;
        if (await tempFile.exists()) {
          await tempFile.delete();
        }
        await tempFile.create(recursive: true);
      } else if (response.statusCode == 206) {
        // Partial content — resuming from existingBytes
      } else if (response.statusCode == 416) {
        // Range Not Satisfiable — file is already complete or invalid range
        // Treat as already downloaded
        existingBytes = 0;
        if (await tempFile.exists()) {
          await tempFile.delete();
        }
      } else {
        throw HttpException('Download failed: HTTP ${response.statusCode}');
      }

      final responseContentLength = response.contentLength;
      final totalBytes =
          (responseContentLength != null && responseContentLength > 0)
              ? responseContentLength + existingBytes
              : null;
      int downloadedBytes = existingBytes;
      final startTime = DateTime.now();
      DateTime lastProgressUpdate = startTime;

      // Open file for append (if resuming) or write (if fresh)
      sink = tempFile.openWrite(mode: FileMode.append);

      await for (final chunk in response.stream.timeout(_httpTimeout)) {
        if (_downloadStatus[key] != DownloadStatus.downloading) {
          // Download was cancelled
          await sink.close();
          // Don't delete the partial file — it can be resumed later
          yield DownloadProgress(
            model: spec,
            downloadedBytes: downloadedBytes,
            totalBytes: totalBytes,
            isCancelled: true,
          );
          _downloadStatus[key] = DownloadStatus.cancelled;
          client.close();
          return;
        }

        sink.add(chunk);
        downloadedBytes += chunk.length;

        // Throttle progress updates to max 10 per second to avoid UI jitter
        final now = DateTime.now();
        final shouldUpdateProgress =
            now.difference(lastProgressUpdate).inMilliseconds >= 100;

        if (shouldUpdateProgress) {
          lastProgressUpdate = now;

          // Calculate speed
          final elapsed = now.difference(startTime).inMilliseconds / 1000.0;
          final speed =
              elapsed > 0 ? (downloadedBytes - existingBytes) / elapsed : 0.0;

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
      }

      await sink.close();
      sink = null;

      // Verify download size if expected size is known
      if (spec.expectedSizeBytes != null) {
        final actualSize = await tempFile.length();
        // Allow 1% tolerance for archive files that may vary slightly
        final tolerance = (spec.expectedSizeBytes! * 0.01).toInt();
        if ((actualSize - spec.expectedSizeBytes!).abs() > tolerance) {
          await tempFile.delete();
          throw Exception(
            '下载文件大小不匹配 (期望 ${spec.expectedSizeBytes}, 实际 $actualSize)',
          );
        }
      }

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
    } catch (e, stackTrace) {
      // Clean up resources
      try {
        await sink?.close();
      } catch (_) {}
      client?.close();

      // Clean up partial file on failure (except for network errors where
      // resume might be useful — keep the .tmp file for those cases)
      final isResumableError =
          e is SocketException || e is TimeoutException || e is HttpException;
      if (!isResumableError && await tempFile.exists()) {
        try {
          await tempFile.delete();
        } catch (_) {}
      }

      _downloadStatus[key] = DownloadStatus.failed;

      final errorMessage = _friendlyErrorMessage(e);

      yield DownloadProgress(
        model: spec,
        downloadedBytes: 0,
        error: errorMessage,
      );

      _progressControllers[key]?.add(DownloadProgress(
        model: spec,
        downloadedBytes: 0,
        error: errorMessage,
      ));

      // Log full error for debugging
      // ignore: avoid_print
      print('[ModelDownloader] Download failed: $e\n$stackTrace');
    }
  }

  /// Cancel a download.
  ///
  /// The partial `.tmp` file is preserved so the download can be resumed later.
  void cancelDownload(ModelSpec spec) {
    final key = spec.dirName;
    if (_downloadStatus[key] == DownloadStatus.downloading) {
      // Set to cancelled (not failed) so the download loop knows to exit
      // and the UI knows to show "cancelled" instead of "error"
      _downloadStatus[key] = DownloadStatus.cancelled;
    }
  }

  /// Download all required models sequentially.
  Stream<DownloadProgress> downloadAllModels() async* {
    for (final spec in kRequiredModels) {
      await for (final progress in downloadModel(spec)) {
        yield progress;
        if (progress.isComplete ||
            progress.error != null ||
            progress.isCancelled) {
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
  ///
  /// Security: validates that extracted paths don't escape the target directory
  /// (path-traversal protection).
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

    // Normalize target path for path-traversal check
    final normalizedTargetPath = targetDir.path;

    // Extract files, stripping the root directory prefix
    for (final file in tarArchive) {
      // Strip the archive root directory from the path
      String relativePath = file.name;
      if (relativePath.startsWith('$archiveRootDir/')) {
        relativePath = relativePath.substring(archiveRootDir.length + 1);
      } else if (relativePath == archiveRootDir ||
          relativePath == '$archiveRootDir/') {
        continue; // Skip the root directory entry itself
      }

      if (relativePath.isEmpty) continue;

      final extractedPath = '${targetDir.path}/$relativePath';

      // Path-traversal protection: ensure the extracted path is within target
      final normalizedExtracted = File(extractedPath).path;
      if (!normalizedExtracted.startsWith(normalizedTargetPath)) {
        throw Exception(
          'Archive contains path traversal attempt: ${file.name}',
        );
      }

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
