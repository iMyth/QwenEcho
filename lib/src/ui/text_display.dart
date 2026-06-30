import 'package:flutter/material.dart';

import 'line_buffer.dart';

/// Color constants for the three text states.
abstract final class TextDisplayColors {
  /// Partial (unconfirmed) ASR text color — gray.
  static const Color partial = Color(0xFF9E9E9E);

  /// Confirmed (finalized) ASR text color — white.
  static const Color confirmed = Color(0xFFFFFFFF);

  /// Translation streaming text color — bright green.
  static const Color translation = Color(0xFF00E676);
}

/// A widget that renders a scrollable list of [DisplayLine] items with
/// three-color state rendering and auto-scroll behavior.
///
/// Listens to a [LineBuffer] and rebuilds whenever lines change.
/// Auto-scrolls to the bottom when new lines are added.
class TextDisplay extends StatefulWidget {
  /// The line buffer providing text lines and change notifications.
  final LineBuffer lineBuffer;

  /// Font size for displayed text.
  final double fontSize;

  /// Optional padding around the text area.
  final EdgeInsetsGeometry padding;

  const TextDisplay({
    super.key,
    required this.lineBuffer,
    this.fontSize = 16.0,
    this.padding = const EdgeInsets.symmetric(horizontal: 12.0, vertical: 8.0),
  });

  @override
  State<TextDisplay> createState() => _TextDisplayState();
}

class _TextDisplayState extends State<TextDisplay> {
  final ScrollController _scrollController = ScrollController();

  @override
  void initState() {
    super.initState();
    widget.lineBuffer.addListener(_onLinesChanged);
  }

  @override
  void didUpdateWidget(TextDisplay oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.lineBuffer != widget.lineBuffer) {
      oldWidget.lineBuffer.removeListener(_onLinesChanged);
      widget.lineBuffer.addListener(_onLinesChanged);
    }
  }

  @override
  void dispose() {
    widget.lineBuffer.removeListener(_onLinesChanged);
    _scrollController.dispose();
    super.dispose();
  }

  void _onLinesChanged() {
    // Trigger rebuild and auto-scroll after frame renders.
    if (mounted) {
      setState(() {});
      WidgetsBinding.instance.addPostFrameCallback((_) {
        _scrollToBottom();
      });
    }
  }

  void _scrollToBottom() {
    if (_scrollController.hasClients) {
      _scrollController.animateTo(
        _scrollController.position.maxScrollExtent,
        duration: const Duration(milliseconds: 100),
        curve: Curves.easeOut,
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final lines = widget.lineBuffer.lines;

    return Padding(
      padding: widget.padding,
      child: ListView.builder(
        controller: _scrollController,
        itemCount: lines.length,
        itemBuilder: (context, index) {
          final line = lines[index];
          return Padding(
            padding: const EdgeInsets.symmetric(vertical: 2.0),
            child: Text(
              line.text,
              style: TextStyle(
                color: _colorForState(line.state),
                fontSize: widget.fontSize,
              ),
            ),
          );
        },
      ),
    );
  }

  /// Maps a [LineState] to its corresponding display color.
  Color _colorForState(LineState state) {
    return switch (state) {
      LineState.partial => TextDisplayColors.partial,
      LineState.confirmed => TextDisplayColors.confirmed,
      LineState.translation => TextDisplayColors.translation,
    };
  }
}
