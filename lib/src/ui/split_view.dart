import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'speaker_half.dart';

/// Full-screen bilateral split view for face-to-face interpretation.
///
/// Divides the screen into two equal halves (top/bottom). The top half is
/// rotated 180 degrees so that the opposing speaker can read it from across
/// the table. Both halves operate independently and simultaneously
/// (full-duplex), each displaying its own ASR text and incoming translation.
///
/// Screen orientation is locked to portrait to prevent layout disruption.
class SplitView extends StatefulWidget {
  const SplitView({super.key});

  @override
  State<SplitView> createState() => SplitViewState();
}

/// Public state for [SplitView], exposing methods to push messages to
/// each speaker half independently.
class SplitViewState extends State<SplitView> {
  /// Global key for the top speaker half (speaker 1, rotated 180°).
  final GlobalKey<SpeakerHalfState> topHalfKey = GlobalKey<SpeakerHalfState>();

  /// Global key for the bottom speaker half (speaker 0, normal orientation).
  final GlobalKey<SpeakerHalfState> bottomHalfKey =
      GlobalKey<SpeakerHalfState>();

  @override
  void initState() {
    super.initState();
    // Lock orientation to portrait mode.
    SystemChrome.setPreferredOrientations([
      DeviceOrientation.portraitUp,
    ]);
    // Hide system UI for full-screen immersive experience.
    SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersiveSticky);
  }

  @override
  void dispose() {
    // Restore default orientations when leaving the split view.
    SystemChrome.setPreferredOrientations([]);
    SystemChrome.setEnabledSystemUIMode(SystemUiMode.edgeToEdge);
    super.dispose();
  }

  /// Add ASR partial (unconfirmed) text to the specified speaker's half.
  void addAsrPartial(int speakerId, String text) {
    final state = _halfStateFor(speakerId);
    state?.updateLastLine(text, kAsrPartialColor);
  }

  /// Add ASR confirmed text to the specified speaker's half.
  void addAsrConfirmed(int speakerId, String text) {
    final state = _halfStateFor(speakerId);
    state?.addLine(text, kAsrConfirmedColor);
  }

  /// Add a translation token to the opposing speaker's half.
  ///
  /// Speaker 0's translation appears in speaker 1's half and vice versa.
  void addTranslation(int speakerId, String text) {
    // Translation from speaker X is displayed in the opposing half.
    final opposingState = _halfStateFor(speakerId == 0 ? 1 : 0);
    opposingState?.addLine(text, kTranslationColor);
  }

  /// Clear both halves.
  void clearAll() {
    topHalfKey.currentState?.clear();
    bottomHalfKey.currentState?.clear();
  }

  SpeakerHalfState? _halfStateFor(int speakerId) {
    return speakerId == 0
        ? bottomHalfKey.currentState
        : topHalfKey.currentState;
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      body: Column(
        children: [
          // Top half — rotated 180° for the opposing speaker.
          Expanded(
            child: Transform.rotate(
              angle: math.pi,
              child: SpeakerHalf(
                key: topHalfKey,
                speakerId: 1,
              ),
            ),
          ),
          // Divider between the two halves.
          Container(
            height: 1.0,
            color: Colors.white24,
          ),
          // Bottom half — normal orientation for this device's holder.
          Expanded(
            child: SpeakerHalf(
              key: bottomHalfKey,
              speakerId: 0,
            ),
          ),
        ],
      ),
    );
  }
}
