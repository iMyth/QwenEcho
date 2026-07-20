/// Language catalog for the interpretation UI.
///
/// Maps ISO 639-1 codes to display labels and native names. Used by the
/// language picker on the home screen and by the engine for translation
/// prompts / TTS voice selection.
library;

/// A supported interpretation language.
class SupportedLanguage {
  /// ISO 639-1 code (e.g. "en", "zh").
  final String code;

  /// English display name (e.g. "English").
  final String englishName;

  /// Native display name (e.g. "English", "中文").
  final String nativeName;

  /// Emoji flag for visual recognition in the picker.
  final String flag;

  const SupportedLanguage({
    required this.code,
    required this.englishName,
    required this.nativeName,
    required this.flag,
  });

  /// Combined label for the picker: "🇺🇸 English (en)".
  String get pickerLabel => '$flag $englishName — $nativeName';
}

/// Languages supported by the on-device interpretation pipeline.
///
/// This list is bounded by the SenseVoice-Small ASR model's coverage and the
/// system TTS voices available on iOS. Adding a language here that the ASR
/// model doesn't recognize will yield silence; adding one that iOS has no
/// voice for will fall back to the default voice.
const List<SupportedLanguage> kSupportedLanguages = <SupportedLanguage>[
  SupportedLanguage(
    code: 'zh',
    englishName: 'Chinese',
    nativeName: '中文',
    flag: '🇨🇳',
  ),
  SupportedLanguage(
    code: 'en',
    englishName: 'English',
    nativeName: 'English',
    flag: '🇺🇸',
  ),
  SupportedLanguage(
    code: 'ja',
    englishName: 'Japanese',
    nativeName: '日本語',
    flag: '🇯🇵',
  ),
  SupportedLanguage(
    code: 'ko',
    englishName: 'Korean',
    nativeName: '한국어',
    flag: '🇰🇷',
  ),
  SupportedLanguage(
    code: 'fr',
    englishName: 'French',
    nativeName: 'Français',
    flag: '🇫🇷',
  ),
  SupportedLanguage(
    code: 'es',
    englishName: 'Spanish',
    nativeName: 'Español',
    flag: '🇪🇸',
  ),
  SupportedLanguage(
    code: 'de',
    englishName: 'German',
    nativeName: 'Deutsch',
    flag: '🇩🇪',
  ),
  SupportedLanguage(
    code: 'ru',
    englishName: 'Russian',
    nativeName: 'Русский',
    flag: '🇷🇺',
  ),
  SupportedLanguage(
    code: 'ar',
    englishName: 'Arabic',
    nativeName: 'العربية',
    flag: '🇸🇦',
  ),
  SupportedLanguage(
    code: 'pt',
    englishName: 'Portuguese',
    nativeName: 'Português',
    flag: '🇵🇹',
  ),
];

/// Look up a language by its ISO code. Returns null if unsupported.
SupportedLanguage? languageForCode(String code) {
  for (final lang in kSupportedLanguages) {
    if (lang.code == code) return lang;
  }
  return null;
}
