import 'dart:async';
import 'dart:io';
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

  // Download URLs (HuggingFace mirror)
  static const String _baseUrl = 'https://huggingface.co/mythchow/qwen-echo-models/resolve/main';

  /// Get the download URL for a model.
  String _getDownloadUrl(ModelSpec spec) {
    // TODO: Configure actual model URLs
    if (spec.kind == ModelKind.asr) {
      return '$_baseUrl/asr/sensevoice-small.tgz';
    } else if (spec.kind == ModelKind.llm) {
      return '$_baseUrl/llm/qwen3.5-0.8b-q4.gguf';
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
        // ASR is a compressed archive - extract it
        // TODO: Implement archive extraction
        // For now, just rename (will need to extract .tgz)
        await Directory(modelPath).create(recursive: true);
        await tempFile.rename('$modelPath/archive.tgz');
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
}
