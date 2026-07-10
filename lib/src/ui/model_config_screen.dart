/// Model configuration & management screen.
///
/// Lists the required MLX models with their on-disk status, and lets the
/// user remove each model. All provisioning is local — no network is used.
/// ASR uses SFSpeechRecognizer (built-in, no model needed); TTS is deferred.
library;

import 'dart:async';

import 'package:flutter/material.dart';

import '../model/model_catalog.dart';
import '../model/model_repository.dart';

/// Full-screen model management page.
class ModelConfigScreen extends StatefulWidget {
  /// Repository used for storage operations. Injectable for testing.
  final ModelRepository repository;

  ModelConfigScreen({super.key, ModelRepository? repository})
      : repository = repository ?? ModelRepository();

  @override
  State<ModelConfigScreen> createState() => _ModelConfigScreenState();
}

class _ModelConfigScreenState extends State<ModelConfigScreen> {
  static const Color _accent = Color(0xFF00E676);

  List<ModelStatus>? _statuses;

  @override
  void initState() {
    super.initState();
    _refresh();
  }

  Future<void> _refresh() async {
    final statuses = await widget.repository.statusAll();
    if (!mounted) return;
    setState(() => _statuses = statuses);
  }

  Future<void> _delete(ModelSpec spec) async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        backgroundColor: const Color(0xFF1E1E1E),
        title: Text('Remove ${spec.displayName}?',
            style: const TextStyle(color: Colors.white)),
        content: const Text(
          'The model file will be deleted from local storage. '
          'You can re-import it later.',
          style: TextStyle(color: Color(0xFFBDBDBD)),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: const Text('Cancel'),
          ),
          TextButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('Remove',
                style: TextStyle(color: Color(0xFFFF5252))),
          ),
        ],
      ),
    );
    if (confirmed != true) return;
    await widget.repository.deleteModel(spec);
    await _refresh();
    _toast('${spec.displayName} removed.');
  }

  void _toast(String message, {bool error = false}) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text(message),
        backgroundColor: error ? const Color(0xFFB00020) : const Color(0xFF323232),
        behavior: SnackBarBehavior.floating,
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final statuses = _statuses;
    return Scaffold(
      backgroundColor: Colors.black,
      appBar: AppBar(
        backgroundColor: Colors.black,
        foregroundColor: Colors.white,
        title: const Text('Model Configuration'),
        elevation: 0,
      ),
      body: statuses == null
          ? const Center(child: CircularProgressIndicator(color: _accent))
          : RefreshIndicator(
              color: _accent,
              backgroundColor: const Color(0xFF1E1E1E),
              onRefresh: _refresh,
              child: ListView(
                padding: const EdgeInsets.all(16),
                children: [
                  _summaryHeader(statuses),
                  const SizedBox(height: 8),
                  for (final status in statuses) _ModelCard(
                    status: status,
                    accent: _accent,
                    onDelete: () => _delete(status.spec),
                    onRefresh: _refresh,
                  ),
                ],
              ),
            ),
    );
  }

  Widget _summaryHeader(List<ModelStatus> statuses) {
    final ready = statuses.where((s) => s.isReady).length;
    final total = statuses.length;
    final allReady = ready == total;
    return Container(
      padding: const EdgeInsets.all(16),
      margin: const EdgeInsets.only(bottom: 12),
      decoration: BoxDecoration(
        color: const Color(0xFF1A1A1A),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(
          color: allReady ? _accent : const Color(0xFF3A3A3A),
        ),
      ),
      child: Row(
        children: [
          Icon(
            allReady ? Icons.verified : Icons.download_for_offline_outlined,
            color: allReady ? _accent : const Color(0xFF9E9E9E),
            size: 28,
          ),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  allReady
                      ? 'All models ready'
                      : 'Models ready: $ready / $total',
                  style: const TextStyle(
                    color: Colors.white,
                    fontSize: 16,
                    fontWeight: FontWeight.w600,
                  ),
                ),
                const SizedBox(height: 2),
                const Text(
                  'MLX models · copy directories into app sandbox · offline',
                  style: TextStyle(color: Color(0xFF9E9E9E), fontSize: 12),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

/// A single model row with status and delete action.
class _ModelCard extends StatelessWidget {
  final ModelStatus status;
  final Color accent;
  final VoidCallback onDelete;
  final VoidCallback onRefresh;

  const _ModelCard({
    required this.status,
    required this.accent,
    required this.onDelete,
    required this.onRefresh,
  });

  @override
  Widget build(BuildContext context) {
    final spec = status.spec;

    return Container(
      margin: const EdgeInsets.only(bottom: 12),
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: const Color(0xFF1A1A1A),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: const Color(0xFF2A2A2A)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              _statusDot(),
              const SizedBox(width: 10),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(spec.displayName,
                        style: const TextStyle(
                          color: Colors.white,
                          fontSize: 15,
                          fontWeight: FontWeight.w600,
                        )),
                    const SizedBox(height: 2),
                    Text(spec.subtitle,
                        style: const TextStyle(
                            color: Color(0xFF9E9E9E), fontSize: 12)),
                  ],
                ),
              ),
              _actionButton(),
            ],
          ),
          const SizedBox(height: 10),
          _detailLine(),
        ],
      ),
    );
  }

  Widget _statusDot() {
    Color color;
    if (status.isReady) {
      color = accent;
    } else if (status.present) {
      color = const Color(0xFFFFB300); // present but invalid/oversized
    } else {
      color = const Color(0xFF616161);
    }
    return Container(
      width: 12,
      height: 12,
      decoration: BoxDecoration(color: color, shape: BoxShape.circle),
    );
  }

  Widget _detailLine() {
    final String text;
    Color color = const Color(0xFF9E9E9E);
    if (!status.present) {
      text = 'Not installed · max ${formatBytes(status.spec.maxSizeBytes)}';
    } else if (!status.validMlx) {
      text = 'Invalid · not an MLX model directory';
      color = const Color(0xFFFF5252);
    } else if (status.exceedsSizeLimit) {
      text = 'Oversized · ${formatBytes(status.sizeBytes)}';
      color = const Color(0xFFFFB300);
    } else {
      text = 'Installed · ${formatBytes(status.sizeBytes)}';
      color = accent;
    }
    return Text(text, style: TextStyle(color: color, fontSize: 12));
  }

  Widget _actionButton() {
    if (status.present) {
      return IconButton(
        tooltip: 'Remove',
        icon: const Icon(Icons.delete_outline, color: Color(0xFFFF5252)),
        onPressed: onDelete,
      );
    }
    return TextButton.icon(
      onPressed: onRefresh,
      icon: const Icon(Icons.refresh, size: 18),
      label: const Text('Refresh'),
      style: TextButton.styleFrom(foregroundColor: accent),
    );
  }
}
