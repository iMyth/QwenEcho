/// Static catalog of the GGUF/INT4 models QwenEcho requires.
///
/// QwenEcho runs three on-device models entirely offline. Model files are
/// provisioned locally (imported from device storage into the app sandbox) —
/// there is NO network download. This catalog defines the expected filename,
/// display metadata, and on-disk size ceiling for each model so the model
/// configuration screen can report status and validate imports.
///
/// The filenames below are the spec defaults; adjust them here if your model
/// artifacts use different names.
library;

/// The kind of model, matching the native engine's `ModelType` ordering
/// (ASR = 0, LLM = 1, TTS = 2).
enum ModelKind {
  /// Automatic Speech Recognition (Qwen3-ASR-0.6B).
  asr,

  /// Bilingual translation LLM (Qwen3-1.7B).
  llm,

  /// Text-to-Speech (Qwen3-TTS-12Hz-0.6B-Base).
  tts,
}

/// Immutable description of a single required model artifact.
class ModelSpec {
  /// Which pipeline stage this model powers.
  final ModelKind kind;

  /// Human-readable name shown in the UI.
  final String displayName;

  /// Model family/version subtitle shown in the UI.
  final String subtitle;

  /// Canonical filename stored inside the app sandbox `models/` directory.
  final String fileName;

  /// Maximum permitted on-disk size in bytes (per Requirement 16).
  final int maxSizeBytes;

  const ModelSpec({
    required this.kind,
    required this.displayName,
    required this.subtitle,
    required this.fileName,
    required this.maxSizeBytes,
  });
}

/// The three models required to run the interpretation pipeline, in pipeline
/// order (ASR → LLM → TTS).
const List<ModelSpec> kRequiredModels = <ModelSpec>[
  ModelSpec(
    kind: ModelKind.asr,
    displayName: 'Qwen3-ASR-0.6B',
    subtitle: 'Speech Recognition · Q4_K · 11 languages',
    fileName: 'qwen3_asr_0.6b_q4_k.gguf',
    maxSizeBytes: 600 * 1024 * 1024, // 577MB actual; 600MB ceiling
  ),
  ModelSpec(
    kind: ModelKind.llm,
    displayName: 'Qwen3-1.7B',
    subtitle: 'Bilingual Translation · Q4_K_M',
    fileName: 'Qwen3-1.7B-Q4_K_M.gguf',
    maxSizeBytes: 1200 * 1024 * 1024, // ~1.1GB actual; 1.2GB ceiling
  ),
  ModelSpec(
    kind: ModelKind.tts,
    displayName: 'Qwen3-TTS-0.6B',
    subtitle: 'Speech Synthesis · Q4_K · 12Hz',
    fileName: 'qwen3-tts-12hz-0.6b-base-q4_k.gguf',
    maxSizeBytes: 560 * 1024 * 1024, // 533MB actual; 560MB ceiling
  ),
];

/// Look up the spec for a given [ModelKind].
ModelSpec specForKind(ModelKind kind) =>
    kRequiredModels.firstWhere((s) => s.kind == kind);
