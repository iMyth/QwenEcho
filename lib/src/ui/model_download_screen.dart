import 'package:flutter/material.dart';
import 'package:connectivity_plus/connectivity_plus.dart';
import 'package:qwen_echo/src/model/model_catalog.dart';
import 'package:qwen_echo/src/model/model_downloader.dart';
import 'package:qwen_echo/src/model/model_storage.dart';

/// Screen that guides users through downloading required AI models.
///
/// Shown on first launch when models are not yet downloaded.
/// Handles WiFi detection, progress display, and error handling.
class ModelDownloadScreen extends StatefulWidget {
  final VoidCallback onComplete;

  const ModelDownloadScreen({super.key, required this.onComplete});

  @override
  State<ModelDownloadScreen> createState() => _ModelDownloadScreenState();
}

class _ModelDownloadScreenState extends State<ModelDownloadScreen> {
  final ModelDownloader _downloader = ModelDownloader.instance;
  final ModelStorage _storage = ModelStorage.instance;

  bool _isChecking = true;
  bool _isDownloading = false;
  bool _showWifiWarning = false;

  final Map<ModelKind, DownloadProgress> _progress = {};
  String? _errorMessage;

  @override
  void initState() {
    super.initState();
    _checkConnectivityAndModels();
  }

  Future<void> _checkConnectivityAndModels() async {
    // Check if models are already downloaded
    final asrReady = await _storage.isModelDownloaded(
      kRequiredModels.firstWhere((s) => s.kind == ModelKind.asr),
    );
    final llmReady = await _storage.isModelDownloaded(
      kRequiredModels.firstWhere((s) => s.kind == ModelKind.llm),
    );

    if (asrReady && llmReady) {
      widget.onComplete();
      return;
    }

    // Check connectivity
    final connectivity = Connectivity();
    final result = await connectivity.checkConnectivity();
    final hasWifi = result.contains(ConnectivityResult.wifi);

    setState(() {
      _isChecking = false;
      _showWifiWarning = !hasWifi;
    });
  }

  Future<void> _startDownload() async {
    setState(() {
      _isDownloading = true;
      _errorMessage = null;
    });

    final asrSpec = kRequiredModels.firstWhere((s) => s.kind == ModelKind.asr);
    final llmSpec = kRequiredModels.firstWhere((s) => s.kind == ModelKind.llm);

    try {
      // Download ASR model
      await for (final progress in _downloader.downloadModel(asrSpec)) {
        if (!mounted) return;
        setState(() {
          _progress[ModelKind.asr] = progress;
        });

        if (progress.error != null) {
          setState(() {
            _errorMessage = 'ASR 模型下载失败: ${progress.error}';
            _isDownloading = false;
          });
          return;
        }

        if (progress.isComplete) break;
      }

      // Download LLM model
      await for (final progress in _downloader.downloadModel(llmSpec)) {
        if (!mounted) return;
        setState(() {
          _progress[ModelKind.llm] = progress;
        });

        if (progress.error != null) {
          setState(() {
            _errorMessage = 'LLM 模型下载失败: ${progress.error}';
            _isDownloading = false;
          });
          return;
        }

        if (progress.isComplete) break;
      }

      // All downloads complete
      widget.onComplete();
    } catch (e) {
      setState(() {
        _errorMessage = '下载失败: $e';
        _isDownloading = false;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    if (_isChecking) {
      return const Scaffold(
        body: Center(
          child: CircularProgressIndicator(),
        ),
      );
    }

    return Scaffold(
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(24.0),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const Spacer(),

              // Title and description
              const Icon(
                Icons.download_for_offline,
                size: 80,
                color: Colors.blue,
              ),
              const SizedBox(height: 24),
              const Text(
                '欢迎使用 QwenEcho',
                style: TextStyle(
                  fontSize: 28,
                  fontWeight: FontWeight.bold,
                ),
                textAlign: TextAlign.center,
              ),
              const SizedBox(height: 12),
              const Text(
                '同声传译需要下载 AI 模型\n总大小: 约 740 MB',
                style: TextStyle(
                  fontSize: 16,
                  color: Colors.grey,
                ),
                textAlign: TextAlign.center,
              ),
              const SizedBox(height: 32),

              // WiFi warning
              if (_showWifiWarning)
                Container(
                  padding: const EdgeInsets.all(16),
                  decoration: BoxDecoration(
                    color: Colors.orange.shade50,
                    borderRadius: BorderRadius.circular(12),
                    border: Border.all(color: Colors.orange.shade200),
                  ),
                  child: Row(
                    children: [
                      Icon(Icons.warning_amber, color: Colors.orange.shade700),
                      const SizedBox(width: 12),
                      const Expanded(
                        child: Text(
                          '💡 建议在 WiFi 环境下下载',
                          style: TextStyle(fontSize: 14),
                        ),
                      ),
                    ],
                  ),
                ),
              if (_showWifiWarning) const SizedBox(height: 16),

              // Progress display (when downloading)
              if (_isDownloading) ...[
                _buildProgressCard(
                  ModelKind.asr,
                  'ASR 模型 (语音识别)',
                  'SenseVoice-Small · 240 MB',
                ),
                const SizedBox(height: 12),
                _buildProgressCard(
                  ModelKind.llm,
                  'LLM 模型 (翻译)',
                  'Qwen3.5-0.8B · 500 MB',
                ),
                const SizedBox(height: 24),
              ],

              // Error message
              if (_errorMessage != null) ...[
                Container(
                  padding: const EdgeInsets.all(16),
                  decoration: BoxDecoration(
                    color: Colors.red.shade50,
                    borderRadius: BorderRadius.circular(12),
                    border: Border.all(color: Colors.red.shade200),
                  ),
                  child: Text(
                    _errorMessage!,
                    style: TextStyle(
                      color: Colors.red.shade700,
                      fontSize: 14,
                    ),
                  ),
                ),
                const SizedBox(height: 16),
              ],

              const Spacer(),

              // Action buttons
              if (!_isDownloading)
                ElevatedButton(
                  onPressed: _startDownload,
                  style: ElevatedButton.styleFrom(
                    padding: const EdgeInsets.symmetric(vertical: 16),
                    textStyle: const TextStyle(
                      fontSize: 18,
                      fontWeight: FontWeight.bold,
                    ),
                  ),
                  child: const Text('下载模型'),
                ),
              if (_isDownloading)
                ElevatedButton(
                  onPressed: () {
                    _downloader.cancelDownload(
                      kRequiredModels.firstWhere((s) => s.kind == ModelKind.asr),
                    );
                    _downloader.cancelDownload(
                      kRequiredModels.firstWhere((s) => s.kind == ModelKind.llm),
                    );
                    setState(() {
                      _isDownloading = false;
                    });
                  },
                  style: ElevatedButton.styleFrom(
                    padding: const EdgeInsets.symmetric(vertical: 16),
                    backgroundColor: Colors.grey.shade300,
                    textStyle: const TextStyle(
                      fontSize: 18,
                      fontWeight: FontWeight.bold,
                      color: Colors.black87,
                    ),
                  ),
                  child: const Text('取消'),
                ),

              const SizedBox(height: 16),

              if (!_isDownloading)
                TextButton(
                  onPressed: () {
                    // Skip for now - will show limited functionality
                    widget.onComplete();
                  },
                  child: const Text('稍后下载（功能受限）'),
                ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildProgressCard(ModelKind kind, String title, String subtitle) {
    final progress = _progress[kind];
    final isComplete = progress?.isComplete ?? false;
    final isDownloading = progress != null && !isComplete;

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Colors.grey.shade50,
        borderRadius: BorderRadius.circular(12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              if (isComplete)
                const Icon(Icons.check_circle, color: Colors.green, size: 20)
              else if (isDownloading)
                const SizedBox(
                  width: 20,
                  height: 20,
                  child: CircularProgressIndicator(strokeWidth: 2),
                )
              else
                Icon(Icons.hourglass_empty, color: Colors.grey.shade400, size: 20),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      title,
                      style: const TextStyle(
                        fontSize: 16,
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                    Text(
                      subtitle,
                      style: TextStyle(
                        fontSize: 12,
                        color: Colors.grey.shade600,
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
          if (progress != null) ...[
            const SizedBox(height: 12),
            LinearProgressIndicator(
              value: progress.progress,
              backgroundColor: Colors.grey.shade200,
              valueColor: AlwaysStoppedAnimation<Color>(
                isComplete ? Colors.green : Colors.blue,
              ),
            ),
            const SizedBox(height: 8),
            Text(
              progress.progressText,
              style: TextStyle(
                fontSize: 12,
                color: Colors.grey.shade600,
              ),
            ),
          ],
        ],
      ),
    );
  }
}
