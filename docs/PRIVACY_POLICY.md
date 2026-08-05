# Privacy Policy — QwenEcho

**Last Updated: August 2026**

## Overview

QwenEcho ("the App") is an on-device simultaneous interpretation application. This privacy policy explains what information the App handles and how it is used.

**TL;DR: QwenEcho does not collect, store, or transmit any personal data. All audio processing happens locally on your device. The only network activity is a one-time download of AI model files during initial setup.**

## Information Collection

### Data NOT Collected

QwenEcho does **not** collect, store, or transmit:

- Personal information (name, email, phone number, etc.)
- Location data
- Contacts or address book
- Usage analytics or telemetry
- Crash reports
- Advertising identifiers (IDFA/GAID)
- Audio recordings — all speech is processed in real-time on-device and immediately discarded after transcription

### One-Time Model Download

On first launch, the App downloads two AI model files (~881 MB total) from GitHub Releases:

- **ASR Model** (~373 MB): SenseVoice-Small for speech recognition
- **LLM Model** (~508 MB): Qwen3.5-0.8B for translation

This download:
- Occurs only once per installation (or if models are manually deleted)
- Uses HTTPS for secure transfer
- Transmits no user data — only the model files are received
- Can be performed over WiFi or cellular (WiFi recommended due to size)
- Is required for the App to function (offline AI inference)

After download, the App operates **entirely offline** and does not make any further network connections.

## Microphone Access

QwenEcho requests microphone access to capture speech for real-time transcription and translation.

- Audio is processed **locally** on your device using on-device AI models
- Audio data is **never** transmitted to any server
- Audio data is **never** stored persistently — it exists only in memory during active interpretation and is discarded immediately after processing
- You can mute the microphone at any time using the in-app mute control

## Third-Party Services

QwenEcho does **not** use:

- Analytics services (no Google Analytics, Firebase Analytics, etc.)
- Crash reporting services (no Crashlytics, Sentry, etc.)
- Advertising services
- Social media integrations
- Third-party SDKs that collect data

The only external resource is the one-time model download from GitHub (github.com), which is a Microsoft-owned software development platform.

## Data Storage

All data processed by QwenEcho remains on your device:

- **AI Models**: Stored in the App's sandboxed directory after download
- **Interpretation text**: Displayed in real-time and not persisted (cleared when you stop interpretation)
- **Settings**: Language preferences stored locally on your device

You can delete the AI models at any time through the App's Model Settings screen. Deleting models will require re-downloading them (~881 MB) to use the App again.

## Children's Privacy

QwenEcho does not knowingly collect any data from anyone, including children under the age of 13. Since no data is collected, there is no concern regarding children's privacy.

## Changes to This Privacy Policy

This privacy policy may be updated from time to time to reflect changes in the App or for legal, technical, or regulatory reasons. The updated policy will be posted within the App and/or on the App Store listing with an updated "Last Updated" date.

## Contact

If you have questions or concerns about this privacy policy, please contact:

- **Developer**: myth
- **Email**: iammyth@icloud.com
- **GitHub**: https://github.com/iMyth/QwenEcho

---

**Summary**: QwenEcho is designed with privacy as a core principle. Your voice stays on your device. No data leaves your phone except the one-time model download during setup.
