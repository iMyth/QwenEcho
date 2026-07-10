/// Persistent status bar for the main interpretation screen.
///
/// Displays:
/// - An always-visible "OFFLINE" badge indicating air-gapped operation
/// - A thermal mode indicator with color coding (Normal/Throttle/Critical)
/// - Integrates [WarningOverlay] for transient memory/latency notifications
///
/// Zero AI logic — all processing is delegated to the Engine.
library;

import 'dart:async';

import 'package:flutter/material.dart';

import '../messages.dart';
import 'warning_overlay.dart';

/// Thermal mode states mirroring the native engine's thermal state machine.
enum ThermalMode {
  normal,
  throttle,
  critical;

  /// Human-readable label for display.
  String get label => switch (this) {
        ThermalMode.normal => 'Normal',
        ThermalMode.throttle => 'Throttle',
        ThermalMode.critical => 'Critical',
      };

  /// Color coding for thermal mode display.
  Color get color => switch (this) {
        ThermalMode.normal => const Color(0xFF00E676), // Green
        ThermalMode.throttle => const Color(0xFFFFA726), // Orange
        ThermalMode.critical => const Color(0xFFFF5252), // Red
      };

  /// Icon for thermal mode display.
  IconData get icon => switch (this) {
        ThermalMode.normal => Icons.thermostat_outlined,
        ThermalMode.throttle => Icons.thermostat,
        ThermalMode.critical => Icons.local_fire_department,
      };

  /// Parse from the native engine's integer thermal mode code.
  ///
  /// 0 = Normal, 1 = Throttle, 2 = Critical.
  static ThermalMode fromCode(int code) => switch (code) {
        0 => ThermalMode.normal,
        1 => ThermalMode.throttle,
        2 => ThermalMode.critical,
        _ => ThermalMode.normal,
      };
}

/// Persistent status bar widget for the interpretation screen.
///
/// Shows the offline badge and thermal state at all times. Overlays transient
/// warnings from memory and latency events.
///
/// Satisfies:
/// - Requirement 12.6: Zero AI logic in UI Shell
/// - Requirement 13.3: Persistent offline-status indicator
class StatusBar extends StatefulWidget {
  /// Stream of messages from the native engine.
  final Stream<EchoMessage> messages;

  const StatusBar({super.key, required this.messages});

  @override
  State<StatusBar> createState() => _StatusBarState();
}

class _StatusBarState extends State<StatusBar> {
  ThermalMode _thermalMode = ThermalMode.normal;
  StreamSubscription<EchoMessage>? _subscription;

  @override
  void initState() {
    super.initState();
    _subscription = widget.messages.listen(_onMessage);
  }

  @override
  void dispose() {
    _subscription?.cancel();
    super.dispose();
  }

  void _onMessage(EchoMessage message) {
    if (message is ThermalStateMessage) {
      final newMode = ThermalMode.fromCode(message.mode);
      if (newMode != _thermalMode) {
        setState(() {
          _thermalMode = newMode;
        });
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Stack(
      children: [
        // Persistent status row: offline badge + thermal indicator.
        SafeArea(
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            child: Row(
              children: [
                _buildOfflineBadge(),
                const SizedBox(width: 8),
                _buildThermalIndicator(),
                const SizedBox(width: 8),
                _buildEngineBadges(),
                const Spacer(),
              ],
            ),
          ),
        ),
        // Transient warning overlay for memory/latency warnings.
        WarningOverlay(messages: widget.messages),
      ],
    );
  }

  /// Persistent "OFFLINE" badge — always visible, indicating air-gapped mode.
  Widget _buildOfflineBadge() {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      decoration: BoxDecoration(
        color: const Color(0xFF1B5E20).withValues(alpha: 0.8),
        borderRadius: BorderRadius.circular(4),
        border: Border.all(color: const Color(0xFF00E676), width: 1),
      ),
      child: const Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(Icons.airplanemode_active, color: Color(0xFF00E676), size: 14),
          SizedBox(width: 4),
          Text(
            'OFFLINE',
            style: TextStyle(
              color: Color(0xFF00E676),
              fontSize: 11,
              fontWeight: FontWeight.w700,
              letterSpacing: 1.2,
            ),
          ),
        ],
      ),
    );
  }

  /// Thermal mode indicator — updates color and label based on thermal state.
  Widget _buildThermalIndicator() {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      decoration: BoxDecoration(
        color: _thermalMode.color.withValues(alpha: 0.15),
        borderRadius: BorderRadius.circular(4),
        border: Border.all(
            color: _thermalMode.color.withValues(alpha: 0.6), width: 1),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(_thermalMode.icon, color: _thermalMode.color, size: 14),
          const SizedBox(width: 4),
          Text(
            _thermalMode.label,
            style: TextStyle(
              color: _thermalMode.color,
              fontSize: 11,
              fontWeight: FontWeight.w600,
            ),
          ),
        ],
      ),
    );
  }

  /// Engine info badges showing which inference engines are active.
  Widget _buildEngineBadges() {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        _engineBadge('ASR', 'Speech'),
        const SizedBox(width: 4),
        _engineBadge('LLM', 'MLX'),
      ],
    );
  }

  Widget _engineBadge(String label, String engine) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 3),
      decoration: BoxDecoration(
        color: const Color(0xFF1A1A1A),
        borderRadius: BorderRadius.circular(4),
        border: Border.all(color: const Color(0xFF3A3A3A), width: 1),
      ),
      child: Text(
        '$label: $engine',
        style: const TextStyle(
          color: Color(0xFFBDBDBD),
          fontSize: 10,
          fontWeight: FontWeight.w500,
        ),
      ),
    );
  }
}
