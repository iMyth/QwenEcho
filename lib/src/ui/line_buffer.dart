import 'package:flutter/foundation.dart';

/// The visual state of a displayed text line, determining its render color.
enum LineState {
  /// Temporary/unconfirmed ASR text — rendered in gray (#9E9E9E).
  partial,

  /// Finalized ASR text — rendered in white (#FFFFFF).
  confirmed,

  /// Streaming translation token — rendered in green (#00E676).
  translation,
}

/// A single line of text in the display buffer.
class DisplayLine {
  /// The text content of this line.
  final String text;

  /// The visual state determining the line's color.
  final LineState state;

  const DisplayLine({required this.text, required this.state});

  DisplayLine copyWith({String? text, LineState? state}) {
    return DisplayLine(
      text: text ?? this.text,
      state: state ?? this.state,
    );
  }

  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      other is DisplayLine && text == other.text && state == other.state;

  @override
  int get hashCode => Object.hash(text, state);

  @override
  String toString() => 'DisplayLine(state=$state, text="$text")';
}

/// Manages a line buffer for one speaker half of the split view.
///
/// Handles three text states (partial, confirmed, translation) and enforces
/// a maximum of [maxLines] lines, discarding the oldest when exceeded.
class LineBuffer extends ChangeNotifier {
  /// Maximum number of lines retained in the buffer.
  static const int maxLines = 50;

  final List<DisplayLine> _lines = [];

  /// Whether there is currently a partial (unconfirmed) ASR line being built.
  bool _hasPartial = false;

  /// Whether there is currently a translation line being built token-by-token.
  bool _hasTranslationInProgress = false;

  /// Unmodifiable view of the current lines.
  List<DisplayLine> get lines => List.unmodifiable(_lines);

  /// Current number of lines in the buffer.
  int get lineCount => _lines.length;

  /// Shows or updates the current partial (gray) ASR text.
  ///
  /// If a partial line already exists, it is replaced with the new text.
  /// Otherwise, a new partial line is appended.
  void addPartialLine(String text) {
    if (_hasPartial) {
      // Replace the existing partial line (always the last partial entry).
      final idx = _lastPartialIndex();
      if (idx >= 0) {
        _lines[idx] = DisplayLine(text: text, state: LineState.partial);
      } else {
        _lines.add(DisplayLine(text: text, state: LineState.partial));
      }
    } else {
      _lines.add(DisplayLine(text: text, state: LineState.partial));
      _hasPartial = true;
    }
    _enforceLimit();
    notifyListeners();
  }

  /// Replaces the current partial line with a confirmed (white) line.
  ///
  /// If no partial line exists, appends a new confirmed line.
  void confirmLine(String text) {
    if (_hasPartial) {
      final idx = _lastPartialIndex();
      if (idx >= 0) {
        _lines[idx] = DisplayLine(text: text, state: LineState.confirmed);
      } else {
        _lines.add(DisplayLine(text: text, state: LineState.confirmed));
      }
      _hasPartial = false;
    } else {
      _lines.add(DisplayLine(text: text, state: LineState.confirmed));
    }
    _enforceLimit();
    notifyListeners();
  }

  /// Appends a translation token to the current translation line (typewriter).
  ///
  /// If no translation line is in progress, creates one. Otherwise appends the
  /// token to the existing translation line.
  void appendTranslationToken(String token) {
    if (_hasTranslationInProgress) {
      final idx = _lastTranslationIndex();
      if (idx >= 0) {
        final existing = _lines[idx];
        _lines[idx] = existing.copyWith(text: existing.text + token);
      } else {
        _lines.add(DisplayLine(text: token, state: LineState.translation));
      }
    } else {
      _lines.add(DisplayLine(text: token, state: LineState.translation));
      _hasTranslationInProgress = true;
    }
    _enforceLimit();
    notifyListeners();
  }

  /// Finalizes the current translation line with the complete text.
  ///
  /// Replaces the in-progress translation line with [fullText] and marks the
  /// translation as complete, so subsequent tokens start a new line.
  void completeTranslation(String fullText) {
    if (_hasTranslationInProgress) {
      final idx = _lastTranslationIndex();
      if (idx >= 0) {
        _lines[idx] = DisplayLine(text: fullText, state: LineState.translation);
      }
    } else {
      _lines.add(DisplayLine(text: fullText, state: LineState.translation));
    }
    _hasTranslationInProgress = false;
    _enforceLimit();
    notifyListeners();
  }

  /// Removes all lines and resets internal state.
  void clear() {
    _lines.clear();
    _hasPartial = false;
    _hasTranslationInProgress = false;
    notifyListeners();
  }

  /// Enforces the [maxLines] limit by discarding oldest lines.
  void _enforceLimit() {
    while (_lines.length > maxLines) {
      _lines.removeAt(0);
    }
  }

  /// Returns the index of the last partial line, or -1 if none found.
  int _lastPartialIndex() {
    for (int i = _lines.length - 1; i >= 0; i--) {
      if (_lines[i].state == LineState.partial) return i;
    }
    return -1;
  }

  /// Returns the index of the last translation line, or -1 if none found.
  int _lastTranslationIndex() {
    for (int i = _lines.length - 1; i >= 0; i--) {
      if (_lines[i].state == LineState.translation) return i;
    }
    return -1;
  }
}
