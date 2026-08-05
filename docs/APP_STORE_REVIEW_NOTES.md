# App Store Review Notes — QwenEcho

**App Name**: QwenEcho — 同声传译 / Simultaneous Interpreter  
**Bundle ID**: com.myth.qwenecho  
**Category**: Utilities / Productivity  
**Supported Languages**: zh, en, ja, ko, fr, es, de, ru, ar, pt  

---

## What the App Does

QwenEcho is a **fully offline** simultaneous interpretation app for face-to-face conversations across language barriers. It transcribes speech in real-time and translates it to the target language, displaying both original and translated text on a split-screen view (one side rotated 180° for the conversation partner to read).

**Core use case**: Two people speaking different languages have a conversation. Each person sees their own language on their side and the other person's translated language on the opposite side.

---

## First-Launch Experience — Important for Reviewers

### One-Time Model Download (~881 MB, WiFi Recommended)

On first launch, the App displays a download screen requesting permission to download two AI model files:

| Model | Size | Purpose |
|---|---|---|
| SenseVoice-Small (ASR) | ~373 MB | Speech recognition (10 languages) |
| Qwen3.5-0.8B (LLM) | ~508 MB | Translation |

**Please note for testing:**
- The download takes 2-10 minutes depending on network speed
- WiFi is strongly recommended (shown to user as a warning)
- Download supports resume — if interrupted, it continues from where it left off
- After download completes, the App is **fully functional offline** with no further network activity
- If testing on simulator, the download still works (uses Mac's network)

### If Models Are Already Downloaded

If you've previously tested the App and models are already downloaded, the download screen will be skipped and you'll go directly to the home screen.

To force the download screen to appear again:
1. Open the App
2. Tap "Settings" on the home screen
3. Tap the trash icon next to each model to delete them
4. Relaunch the App

---

## Downloaded Content — Clarification

The ~881 MB downloaded on first launch consists of **static AI model weight files** (not executable code):

- **`.gguf` file**: Quantized neural network weights for the translation LLM (Qwen3.5-0.8B, Apache-2.0 licensed)
- **`.onnx` files + supporting files**: Neural network weights for the speech recognition model (SenseVoice-Small, Apache-2.0 licensed)

These files:
- Are **not executable code** — they are data files consumed by on-device inference engines (llama.cpp for GGUF, sherpa-onnx for ONNX)
- Are **not updated remotely** — the download URLs are pinned to a specific GitHub release version (v0.1.0). If model versions change in the future, the App will need to be resubmitted with updated URLs.
- Do **not contain any user data** — they are read-only model weights
- Are **required for the App to function** — without them, no speech recognition or translation is possible

The source code for both models is publicly available:
- Qwen3.5: https://github.com/QwenLM/Qwen2.5
- SenseVoice: https://github.com/FunAudioLLM/SenseVoice

---

## Privacy & Data Handling

**QwenEcho does not collect, store, or transmit any personal data.**

- ✅ **No account creation** — no login, no email, no phone number
- ✅ **No analytics or telemetry** — no usage tracking, no crash reporting SDKs
- ✅ **No advertising** — no ads, no ad identifiers
- ✅ **No location tracking** — no location services used
- ✅ **Audio processed 100% on-device** — microphone input is transcribed locally and immediately discarded; never stored, never transmitted
- ✅ **Fully offline after initial download** — once models are downloaded, the App requires zero network connectivity

The only network activity is the **one-time model download** from GitHub Releases (HTTPS).

Full privacy policy: [docs/PRIVACY_POLICY.md](./PRIVACY_POLICY.md)

---

## Testing the App

### Prerequisites
- WiFi connection (for initial ~881 MB download)
- iPhone or iPad (or simulator with Mac microphone passthrough enabled)

### Step-by-Step Test Flow

1. **Launch the App** — Download screen appears (if models not yet downloaded)
2. **Tap "下载模型" (Download Models)** — Progress bars show download status for ASR and LLM models
3. **Wait for download to complete** (~2-10 minutes on WiFi)
4. **Home screen appears** — Language pair selection (default: Chinese → English)
5. **Tap "Start Interpreting"** — Microphone permission prompt appears
6. **Grant microphone permission** — Split-screen interpretation view appears
7. **Speak into the microphone** — Original text appears on your side, translation appears on the partner's side (rotated 180°)
8. **Test features**:
   - Swap languages (↔ button)
   - Mute microphone (🎤 button)
   - Stop interpretation (⏹ button)
   - Model Settings (trash icon) — view/delete models

### Simulator Notes
- On iOS Simulator, enable Mac microphone passthrough: **Features → Audio Input → [Your Mac's Mic]**
- Without passthrough, the App uses synthetic silence (useful for testing UI flow but not actual interpretation)

---

## Known Limitations

1. **First launch requires ~881 MB download** — this is unavoidable for offline AI inference. The App warns users and recommends WiFi.
2. **"Skip download" leads to limited functionality** — users can skip the download but the App will not function until models are downloaded (can be resumed from Model Settings).
3. **Translation quality** — Qwen3.5-0.8B is a small model optimized for mobile; translation quality is good for casual conversation but not professional-grade.
4. **TTS uses system voices** — Text-to-speech quality varies by device and OS. iOS voices are generally good; Android voices vary by manufacturer.
5. **No background audio** — interpretation stops when the App is backgrounded (this is by design to preserve battery and privacy).

---

## Technical Architecture (For Reviewer Reference)

| Component | Technology | Location |
|---|---|---|
| Speech Recognition (ASR) | sherpa-onnx + SenseVoice-Small | On-device, offline |
| Translation (LLM) | llama.cpp + Qwen3.5-0.8B (GGUF) | On-device, offline |
| Text-to-Speech (TTS) | iOS AVSpeechSynthesizer / Android TTS | System voices |
| Audio Capture | AVAudioEngine (iOS) / AudioRecord (Android) | On-device |
| Voice Activity Detection | Energy-based VAD | On-device |
| Network | Only for initial model download (HTTPS to GitHub) | One-time |

All inference runs on-device. No server-side processing. No cloud APIs.

---

## Model Licenses

Both models used by QwenEcho are open-source under permissive licenses:

- **Qwen3.5-0.8B**: Apache License 2.0 — https://github.com/QwenLM/Qwen2.5
- **SenseVoice-Small**: Apache License 2.0 — https://github.com/FunAudioLLM/SenseVoice

The App itself is also open-source: https://github.com/iMyth/QwenEcho

---

## Contact

For questions during review:
- **Developer**: myth
- **Email**: iammyth@icloud.com
- **GitHub**: https://github.com/iMyth/QwenEcho
