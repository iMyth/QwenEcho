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
/// - TTS: handled by iOS system voices (AVSpeechSynthesizer) — no model file
///   required, so [ModelKind.tts] has no [ModelSpec] in [kRequiredModels].
library;

/// The kind of model, matching the pipeline stage order (ASR = 0, LLM = 1, TTS = 2).
enum ModelKind {
  /// Automatic Speech Recognition (sherpa-onnx model package).
  asr,

  /// Bilingual translation LLM (Qwen3.5-0.8B via llamadart/llama.cpp GGUF).
  llm,

  /// Text-to-Speech — handled by iOS system voices, no on-disk model.
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

  /// Expected file size in bytes for download validation.
  /// Used to verify download completeness and for disk-space preflight.
  final int? expectedSizeBytes;

  const ModelSpec({
    required this.kind,
    required this.displayName,
    required this.subtitle,
    required this.dirName,
    required this.maxSizeBytes,
    this.expectedSizeBytes,
  });
}

/// The models required to run the interpretation pipeline.
///
/// Both the ASR package and the LLM GGUF file must be imported locally.
/// TTS uses iOS system voices and requires no model file.
const List<ModelSpec> kRequiredModels = <ModelSpec>[
  ModelSpec(
    kind: ModelKind.asr,
    displayName: 'SenseVoice-Small',
    subtitle: 'Offline ASR · sherpa-onnx · 373 MB',
    dirName: 'SenseVoiceSmall-onnx',
    maxSizeBytes: 400 * 1024 * 1024, // ~373MB actual; 400MB ceiling
    expectedSizeBytes: 391000000, // ~373MB archive (approximate)
  ),
  ModelSpec(
    kind: ModelKind.llm,
    displayName: 'Qwen3.5-0.8B',
    subtitle: 'Bilingual Translation · llama.cpp GGUF · 508 MB',
    dirName: 'Qwen3.5-0.8B-Q4_K_M.gguf',
    maxSizeBytes: 600 * 1024 * 1024, // ~508MB expected; 600MB ceiling
    expectedSizeBytes: 532000000, // ~508MB (approximate)
  ),
];

/// Look up the spec for a given [ModelKind].
ModelSpec specForKind(ModelKind kind) =>
    kRequiredModels.firstWhere((s) => s.kind == kind);
