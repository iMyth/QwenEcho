/// Static catalog of the MLX models QwenEcho requires.
///
/// QwenEcho runs on-device inference entirely offline. The LLM model file is
/// provisioned locally (imported from device storage into the app sandbox) —
/// there is NO network download. This catalog defines the expected directory
/// name, display metadata, and on-disk size ceiling for each model so the model
/// configuration screen can report status and validate imports.
///
/// MLX models are directories containing config.json, model.safetensors,
/// and tokenizer.json.
library;

/// The kind of model, matching the pipeline stage order (ASR = 0, LLM = 1, TTS = 2).
enum ModelKind {
  /// Automatic Speech Recognition (SFSpeechRecognizer — no model file needed).
  asr,

  /// Bilingual translation LLM (Qwen3-1.7B via MLX).
  llm,

  /// Text-to-Speech (deferred — Phase 5).
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

  /// Canonical directory name stored inside the app sandbox `models/` directory.
  /// An MLX model is a directory containing config.json, model.safetensors, etc.
  final String dirName;

  /// Maximum permitted on-disk size in bytes (per Requirement 16).
  final int maxSizeBytes;

  const ModelSpec({
    required this.kind,
    required this.displayName,
    required this.subtitle,
    required this.dirName,
    required this.maxSizeBytes,
  });
}

/// The models required to run the interpretation pipeline.
///
/// Only the LLM model needs to be imported — ASR uses SFSpeechRecognizer
/// (built into iOS, no model file needed) and TTS is deferred to Phase 5.
const List<ModelSpec> kRequiredModels = <ModelSpec>[
  ModelSpec(
    kind: ModelKind.llm,
    displayName: 'Qwen3-1.7B-4bit',
    subtitle: 'Bilingual Translation · MLX 4-bit',
    dirName: 'qwen3-1.7b-4bit-mlx',
    maxSizeBytes: 1200 * 1024 * 1024, // ~1GB expected; 1.2GB ceiling
  ),
];

/// Look up the spec for a given [ModelKind].
ModelSpec specForKind(ModelKind kind) =>
    kRequiredModels.firstWhere((s) => s.kind == kind);
