import 'package:flutter/material.dart';

/// Maximum number of displayed lines per speaker half.
const int kMaxLines = 50;

/// Idle indicator text shown when no messages have been received.
const String kIdleText = 'Waiting for speech...';

/// Color for temporary (unconfirmed) ASR text.
const Color kAsrPartialColor = Color(0xFF9E9E9E);

/// Color for confirmed ASR text.
const Color kAsrConfirmedColor = Color(0xFFFFFFFF);

/// Color for translation text (streaming tokens).
const Color kTranslationColor = Color(0xFF00E676);

/// A single colored text line displayed in the speaker half.
@immutable
class DisplayLine {
  /// The text content of this line.
  final String text;

  /// The color to render this line in.
  final Color color;

  const DisplayLine({required this.text, required this.color});
}

/// Widget representing one half of the bilateral split view.
///
/// Displays the speaker's own ASR text (partial/confirmed) and
/// incoming translation from the opposing speaker. Shows an idle
/// indicator when no text has been received.
class SpeakerHalf extends StatefulWidget {
  /// Identifier for this speaker (0 = bottom, 1 = top).
  final int speakerId;

  const SpeakerHalf({super.key, required this.speakerId});

  @override
  State<SpeakerHalf> createState() => SpeakerHalfState();
}

/// Public state for [SpeakerHalf], allowing the parent to push lines.
class SpeakerHalfState extends State<SpeakerHalf> {
  final List<DisplayLine> _lines = [];
  final ScrollController _scrollController = ScrollController();

  /// Whether this half has received any content.
  bool get isEmpty => _lines.isEmpty;

  /// Current number of lines in the buffer.
  int get lineCount => _lines.length;

  /// Add a line of text with the given color.
  ///
  /// If the line buffer exceeds [kMaxLines], the oldest lines are discarded.
  void addLine(String text, Color color) {
    setState(() {
      _lines.add(DisplayLine(text: text, color: color));
      // Enforce the 50-line limit by discarding oldest.
      while (_lines.length > kMaxLines) {
        _lines.removeAt(0);
      }
    });
    _scrollToBottom();
  }

  /// Replace the last line (used for updating partial ASR text in-place).
  void updateLastLine(String text, Color color) {
    setState(() {
      if (_lines.isNotEmpty &&
          _lines.last.color == kAsrPartialColor &&
          color == kAsrPartialColor) {
        _lines[_lines.length - 1] = DisplayLine(text: text, color: color);
      } else {
        _lines.add(DisplayLine(text: text, color: color));
        while (_lines.length > kMaxLines) {
          _lines.removeAt(0);
        }
      }
    });
    _scrollToBottom();
  }

  /// Clear all lines and reset to idle state.
  void clear() {
    setState(() {
      _lines.clear();
    });
  }

  void _scrollToBottom() {
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (_scrollController.hasClients) {
        _scrollController.animateTo(
          _scrollController.position.maxScrollExtent,
          duration: const Duration(milliseconds: 100),
          curve: Curves.easeOut,
        );
      }
    });
  }

  @override
  void dispose() {
    _scrollController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      color: Colors.black,
      padding: const EdgeInsets.symmetric(horizontal: 16.0, vertical: 8.0),
      child: _lines.isEmpty ? _buildIdleIndicator() : _buildTextList(),
    );
  }

  Widget _buildIdleIndicator() {
    return const Center(
      child: Text(
        kIdleText,
        style: TextStyle(
          color: kAsrPartialColor,
          fontSize: 18.0,
          fontStyle: FontStyle.italic,
        ),
      ),
    );
  }

  Widget _buildTextList() {
    return ListView.builder(
      controller: _scrollController,
      itemCount: _lines.length,
      itemBuilder: (context, index) {
        final line = _lines[index];
        return Padding(
          padding: const EdgeInsets.symmetric(vertical: 2.0),
          child: Text(
            line.text,
            style: TextStyle(
              color: line.color,
              fontSize: 16.0,
            ),
          ),
        );
      },
    );
  }
}
