/// Static catalog of the models QwenEcho requires.
///
/// QwenEcho runs on-device inference entirely offline. Models are provisioned
/// locally (imported from device storage into the app sandbox) — there is NO
/// network download. This catalog defines the expected file or directory name,
/// display metadata, and on-disk size ceiling for each model so the model
/// configuration screen can report status and validate imports.
///
/// - LLM: a single GGUF file (`*.gguf`) consumed by llamadart.
/// - ASR: a SenseVoice-Small ONNX model package directory containing
///   `model.int8.onnx` and `tokens.txt`.
/// - TTS: deferred to Phase 5.
library;

/// The kind of model, matching the pipeline stage order (ASR = 0, LLM = 1, TTS = 2).
enum ModelKind {
  /// Automatic Speech Recognition (sherpa-onnx model package).
  asr,

  /// Bilingual translation LLM (Qwen3.5-0.8B via llamadart/llama.cpp GGUF).
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

  /// Canonical file or directory name stored inside the app sandbox `models/`
  /// directory. For GGUF this is a filename; for sherpa-onnx it is a directory.
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
/// Both the ASR package and the LLM GGUF file must be imported locally.
/// TTS is deferred to Phase 5.
const List<ModelSpec> kRequiredModels = <ModelSpec>[
  ModelSpec(
    kind: ModelKind.asr,
    displayName: 'SenseVoice-Small',
    subtitle: 'Offline ASR · sherpa-onnx',
    dirName: 'SenseVoiceSmall-onnx',
    maxSizeBytes: 300 * 1024 * 1024, // ~241MB expected; 300MB ceiling
  ),
  ModelSpec(
    kind: ModelKind.llm,
    displayName: 'Qwen3.5-0.8B',
    subtitle: 'Bilingual Translation · llama.cpp GGUF',
    dirName: 'Qwen3.5-0.8B-Q4_K_M.gguf',
    maxSizeBytes: 600 * 1024 * 1024, // ~513MB expected; 600MB ceiling
  ),
];

/// Look up the spec for a given [ModelKind].
ModelSpec specForKind(ModelKind kind) =>
    kRequiredModels.firstWhere((s) => s.kind == kind);
