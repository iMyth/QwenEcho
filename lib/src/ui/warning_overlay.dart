/// Transient warning overlay for memory and latency warnings.
///
/// Displays auto-dismissing notifications that fade out after a configurable
/// duration. All logic is pure UI rendering — no AI processing.
library;

import 'dart:async';

import 'package:flutter/material.dart';

import '../messages.dart';

/// A single warning entry displayed in the overlay.
@immutable
class _WarningEntry {
  final String message;
  final Color color;
  final DateTime createdAt;
  final Duration displayDuration;

  const _WarningEntry({
    required this.message,
    required this.color,
    required this.createdAt,
    required this.displayDuration,
  });

  bool isExpired(DateTime now) => now.difference(createdAt) >= displayDuration;
}

/// Overlay widget that shows transient warning notifications.
///
/// Listens to a stream of [EchoMessage] and displays memory warnings and
/// latency warnings as banners that auto-dismiss after [displayDuration].
///
/// Zero AI logic — all processing is delegated to the Engine.
class WarningOverlay extends StatefulWidget {
  /// Stream of messages from the native engine.
  final Stream<EchoMessage> messages;

  /// How long each warning remains visible before auto-dismissing.
  final Duration displayDuration;

  /// Maximum number of concurrent warnings shown.
  final int maxVisible;

  /// Clock function for obtaining current time. Defaults to [DateTime.now].
  /// Override in tests to use a controllable clock.
  final DateTime Function() clock;

  const WarningOverlay({
    super.key,
    required this.messages,
    this.displayDuration = const Duration(seconds: 4),
    this.maxVisible = 3,
    this.clock = _defaultClock,
  });

  static DateTime _defaultClock() => DateTime.now();

  @override
  State<WarningOverlay> createState() => _WarningOverlayState();
}

class _WarningOverlayState extends State<WarningOverlay> {
  final List<_WarningEntry> _warnings = [];
  StreamSubscription<EchoMessage>? _subscription;
  Timer? _cleanupTimer;

  @override
  void initState() {
    super.initState();
    _subscription = widget.messages.listen(_onMessage);
    _cleanupTimer = Timer.periodic(
      const Duration(seconds: 1),
      (_) => _removeExpired(),
    );
  }

  @override
  void dispose() {
    _subscription?.cancel();
    _cleanupTimer?.cancel();
    super.dispose();
  }

  void _onMessage(EchoMessage message) {
    switch (message) {
      case MemoryWarningMessage():
        _addWarning(
          message: _formatMemoryWarning(message),
          color: message.level >= 2
              ? const Color(0xFFFF5252) // Red for critical
              : const Color(0xFFFFA726), // Orange for warning
        );
      case LatencyWarningMessage():
        _addWarning(
          message: _formatLatencyWarning(message),
          color: const Color(0xFFFFD54F), // Amber for latency
        );
      default:
        break;
    }
  }

  void _addWarning({required String message, required Color color}) {
    setState(() {
      _warnings.add(_WarningEntry(
        message: message,
        color: color,
        createdAt: widget.clock(),
        displayDuration: widget.displayDuration,
      ));
      // Trim to max visible count.
      while (_warnings.length > widget.maxVisible) {
        _warnings.removeAt(0);
      }
    });
  }

  void _removeExpired() {
    final now = widget.clock();
    final hadExpired = _warnings.any((w) => w.isExpired(now));
    if (hadExpired) {
      setState(() {
        _warnings.removeWhere((w) => w.isExpired(now));
      });
    }
  }

  String _formatMemoryWarning(MemoryWarningMessage msg) {
    final pct = msg.usagePercent.toStringAsFixed(0);
    if (msg.level >= 2) {
      return 'CRITICAL: Memory at $pct% — pipeline may stop';
    }
    return 'Memory warning: usage at $pct%';
  }

  String _formatLatencyWarning(LatencyWarningMessage msg) {
    return '${msg.stage} latency: ${msg.actualMs}ms (budget: ${msg.budgetMs}ms)';
  }

  @override
  Widget build(BuildContext context) {
    if (_warnings.isEmpty) return const SizedBox.shrink();

    return Positioned(
      top: MediaQuery.of(context).padding.top + 60,
      left: 16,
      right: 16,
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: _warnings.map((entry) {
          final elapsed = widget.clock().difference(entry.createdAt);
          final remaining = entry.displayDuration - elapsed;
          final opacity = remaining.inMilliseconds > 1000
              ? 1.0
              : (remaining.inMilliseconds / 1000.0).clamp(0.0, 1.0);

          return Padding(
            padding: const EdgeInsets.only(bottom: 4),
            child: Opacity(
              opacity: opacity,
              child: Container(
                width: double.infinity,
                padding:
                    const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                decoration: BoxDecoration(
                  color: entry.color.withValues(alpha: 0.15),
                  borderRadius: BorderRadius.circular(8),
                  border:
                      Border.all(color: entry.color.withValues(alpha: 0.6)),
                ),
                child: Row(
                  children: [
                    Icon(Icons.warning_amber_rounded,
                        color: entry.color, size: 18),
                    const SizedBox(width: 8),
                    Expanded(
                      child: Text(
                        entry.message,
                        style: TextStyle(
                          color: entry.color,
                          fontSize: 12,
                          fontWeight: FontWeight.w500,
                        ),
                        maxLines: 2,
                        overflow: TextOverflow.ellipsis,
                      ),
                    ),
                  ],
                ),
              ),
            ),
          );
        }).toList(),
      ),
    );
  }
}
