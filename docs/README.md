# QwenEcho Documentation

This directory contains documentation for QwenEcho app submission and maintenance.

## Files

### For App Store Submission

- **`PRIVACY_POLICY.md`** — Full privacy policy in Markdown format
- **`APP_STORE_REVIEW_NOTES.md`** — Detailed notes for App Store reviewers explaining the app's functionality, model download process, and privacy architecture
- **`web/privacy.html`** — Bilingual (English/Chinese) privacy policy HTML page for hosting

### Hosting the Privacy Policy

To host the privacy policy for App Store Connect:

1. **Option A: GitHub Pages** (Recommended)
   - Enable GitHub Pages for this repository: Settings → Pages → Source: Deploy from branch → `main` → `/docs` folder
   - Privacy policy URL: `https://<your-username>.github.io/QwenEcho/web/privacy.html`
   - Example: `https://imyths.github.io/QwenEcho/web/privacy.html`

2. **Option B: Static Hosting**
   - Upload `web/privacy.html` to any static hosting service (Netlify, Vercel, GitHub Pages, etc.)
   - Use the hosted URL in App Store Connect

### Before Submission Checklist

- [ ] Replace `[your-email@example.com]` in `PRIVACY_POLICY.md` and `web/privacy.html` with your actual contact email
- [ ] Host `web/privacy.html` and get the public URL
- [ ] Fill in App Store Connect metadata:
  - Bundle ID: `com.myth.qwenecho`
  - Privacy Policy URL: (the hosted URL)
  - App description, screenshots, keywords
- [ ] Copy content from `APP_STORE_REVIEW_NOTES.md` into the "Notes for Reviewer" field in App Store Connect
- [ ] Set App Privacy details to "Data Not Collected"

### Model Licenses

Both AI models used by QwenEcho are open-source under Apache 2.0:

- **Qwen3.5-0.8B**: https://github.com/QwenLM/Qwen2.5
- **SenseVoice-Small**: https://github.com/FunAudioLLM/SenseVoice

### Technical Architecture

See `APP_STORE_REVIEW_NOTES.md` for detailed technical architecture documentation suitable for App Store reviewers.
