/// Model configuration & management screen.
///
/// Lists the three required GGUF models (ASR / LLM / TTS) with their on-disk
/// status, and lets the user import each model from local device storage or
/// remove it. All provisioning is local — no network is used.
library;

import 'dart:async';

import 'package:file_selector/file_selector.dart';
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

  /// Per-model in-flight import progress, keyed by [ModelKind].
  final Map<ModelKind, double> _importing = <ModelKind, double>{};

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

  Future<void> _import(ModelSpec spec) async {
    // Pick any file via the system document picker. We deliberately do NOT
    // filter by extension because .gguf has no registered iOS UTI — passing
    // an extension filter makes UIDocumentPickerViewController fail silently
    // on iOS. The repository validates the GGUF magic bytes and size instead.
    final XFile? file = await openFile();
    final sourcePath = file?.path;
    if (sourcePath == null) return; // user cancelled

    setState(() => _importing[spec.kind] = 0.0);

    try {
      await for (final p in widget.repository.importModel(spec, sourcePath)) {
        if (!mounted) return;
        setState(() => _importing[spec.kind] = p.fraction);
      }
      await _refresh();
      _toast('${spec.displayName} imported.');
    } on ModelImportException catch (e) {
      _toast(e.message, error: true);
    } catch (e) {
      _toast('Import failed: $e', error: true);
    } finally {
      if (mounted) setState(() => _importing.remove(spec.kind));
    }
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
                    progress: _importing[status.spec.kind],
                    accent: _accent,
                    onImport: () => _import(status.spec),
                    onDelete: () => _delete(status.spec),
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
                  'Import GGUF/INT4 files from local storage · offline',
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

/// A single model row with status and import/delete actions.
class _ModelCard extends StatelessWidget {
  final ModelStatus status;
  final double? progress; // null = not importing
  final Color accent;
  final VoidCallback onImport;
  final VoidCallback onDelete;

  const _ModelCard({
    required this.status,
    required this.progress,
    required this.accent,
    required this.onImport,
    required this.onDelete,
  });

  @override
  Widget build(BuildContext context) {
    final spec = status.spec;
    final importing = progress != null;

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
          if (importing) ...[
            const SizedBox(height: 10),
            ClipRRect(
              borderRadius: BorderRadius.circular(4),
              child: LinearProgressIndicator(
                value: progress,
                minHeight: 6,
                backgroundColor: const Color(0xFF2A2A2A),
                valueColor: AlwaysStoppedAnimation(accent),
              ),
            ),
            const SizedBox(height: 4),
            Text('Importing… ${((progress ?? 0) * 100).toStringAsFixed(0)}%',
                style: TextStyle(color: accent, fontSize: 11)),
          ],
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
    } else if (!status.validGguf) {
      text = 'Invalid file · not a GGUF model';
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
    if (progress != null) {
      return const SizedBox(
        width: 20,
        height: 20,
        child: CircularProgressIndicator(strokeWidth: 2, color: Color(0xFF00E676)),
      );
    }
    if (status.present) {
      return IconButton(
        tooltip: 'Remove',
        icon: const Icon(Icons.delete_outline, color: Color(0xFFFF5252)),
        onPressed: onDelete,
      );
    }
    return TextButton.icon(
      onPressed: onImport,
      icon: const Icon(Icons.folder_open, size: 18),
      label: const Text('Import'),
      style: TextButton.styleFrom(foregroundColor: accent),
    );
  }
}
