---
kind: frontend_style
name: Flutter Material Dark UI with Inline Styles
category: frontend_style
scope:
    - '**'
source_files:
    - lib/main.dart
    - lib/qwen_echo.dart
    - lib/src/ui/split_view.dart
    - lib/src/ui/status_bar.dart
    - pubspec.yaml
---

QwenEcho's frontend uses a minimal Flutter Material Design approach with no external styling framework, CSS, or design-token system. All visual styling is defined inline in Dart widgets using `ThemeData`, `Colors`, and direct `TextStyle`/`BoxDecoration` parameters.

**Styling system**: The app bootstraps with `MaterialApp(theme: ThemeData.dark().copyWith(scaffoldBackgroundColor: Colors.black))` and overrides `Scaffold.backgroundColor: Colors.black` throughout the hierarchy. There are no `.css`, `.scss`, `styled_*.dart`, or theme files — every color, font size, border radius, and icon is specified directly in widget constructors.

**Color conventions**: Hardcoded hex colors (e.g. `Color(0xFF00E676)` for green, `Color(0xFFFFA726)` for orange, `Color(0xFFFF5252)` for red) are used consistently across status indicators and badges rather than being centralized in a palette. Text styles use explicit `fontSize`, `fontWeight`, and `letterSpacing` values per component.

**Layout approach**: Pure Flutter layout primitives (`Column`, `Row`, `Expanded`, `Stack`, `Positioned`, `SafeArea`) compose the split-screen interpretation view. Orientation is locked to portrait via `SystemChrome.setPreferredOrientations([DeviceOrientation.portraitUp])` and system UI is hidden with `SystemUiMode.immersiveSticky` for full-screen immersion.

**Component structure**: UI lives under `lib/src/ui/` as small focused widgets (`split_view.dart`, `speaker_half.dart`, `status_bar.dart`, `text_display.dart`, `warning_overlay.dart`, `model_config_screen.dart`). Each file is self-contained with its own inline styles; there is no shared style sheet or theming layer beyond the root `ThemeData.dark()`.

**No external styling dependencies**: `pubspec.yaml` lists only `flutter` SDK, `ffi`, `path_provider`, and `file_picker`. No `provider`, `riverpod`, `go_router`, `google_fonts`, `flutter_svg`, or styling packages are used.