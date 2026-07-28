#!/bin/bash
#
# Download and verify QwenEcho models.
#
# This script provisions the two models required for offline operation:
#   1. SenseVoice-Small ONNX (ASR) — from HuggingFace (public)
#   2. Qwen3.5-0.8B-Q4_K_M GGUF (LLM) — from GitHub Releases (this repo)
#
# Usage:
#   bash scripts/setup_models.sh
#
# After running, the models/ directory will contain:
#   models/SenseVoiceSmall-onnx/model.int8.onnx
#   models/SenseVoiceSmall-onnx/tokens.txt
#   models/Qwen3.5-0.8B-Q4_K_M.gguf
#
# The app can then be built and run without network access.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
MODELS_DIR="$PROJECT_ROOT/models"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================="
echo " QwenEcho Model Setup"
echo "========================================="
echo ""

# Create models directory
mkdir -p "$MODELS_DIR/SenseVoiceSmall-onnx"

# ============================================================================
# 1. SenseVoice-Small ONNX (ASR)
# ============================================================================
SENSEVOICE_DIR="$MODELS_DIR/SenseVoiceSmall-onnx"
SENSEVOICE_ONNX="model.int8.onnx"
SENSEVOICE_TOKENS="tokens.txt"
SENSEVOICE_ONNX_URL="https://huggingface.co/csukuangfj/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/resolve/main/$SENSEVOICE_ONNX?download=true"
SENSEVOICE_TOKENS_URL="https://huggingface.co/csukuangfj/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/resolve/main/$SENSEVOICE_TOKENS?download=true"

# Checksums (MD5) — verify after download
SENSEVOICE_ONNX_MD5="0e8e550411d3589be9f0af06f1c8fec3"
# tokens.txt checksum may vary; we'll just check it exists

echo ""
echo -e "${YELLOW}[1/2] SenseVoice-Small ONNX (ASR)${NC}"
echo "  Source: HuggingFace (csukuangfj/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17)"
echo ""

# Download model.int8.onnx
if [ -f "$SENSEVOICE_DIR/$SENSEVOICE_ONNX" ]; then
    echo "  ✓ $SENSEVOICE_ONNX already exists, verifying checksum..."
    EXISTING_MD5=$(md5 -q "$SENSEVOICE_DIR/$SENSEVOICE_ONNX" 2>/dev/null || md5sum "$SENSEVOICE_DIR/$SENSEVOICE_ONNX" | awk '{print $1}')
    if [ "$EXISTING_MD5" = "$SENSEVOICE_ONNX_MD5" ]; then
        echo -e "  ${GREEN}✓ Checksum matches${NC}"
    else
        echo -e "  ${RED} Checksum mismatch (expected $SENSEVOICE_ONNX_MD5, got $EXISTING_MD5)${NC}"
        echo "  Re-downloading..."
        curl -L --progress-bar "$SENSEVOICE_ONNX_URL" -o "$SENSEVOICE_DIR/$SENSEVOICE_ONNX"
    fi
else
    echo "  Downloading $SENSEVOICE_ONNX (228MB)..."
    curl -L --progress-bar "$SENSEVOICE_ONNX_URL" -o "$SENSEVOICE_DIR/$SENSEVOICE_ONNX"
    echo "  Verifying checksum..."
    DOWNLOADED_MD5=$(md5 -q "$SENSEVOICE_DIR/$SENSEVOICE_ONNX" 2>/dev/null || md5sum "$SENSEVOICE_DIR/$SENSEVOICE_ONNX" | awk '{print $1}')
    if [ "$DOWNLOADED_MD5" = "$SENSEVOICE_ONNX_MD5" ]; then
        echo -e "  ${GREEN}✓ Checksum matches${NC}"
    else
        echo -e "  ${RED}✗ Checksum mismatch! File may be corrupted.${NC}"
        echo "  Expected: $SENSEVOICE_ONNX_MD5"
        echo "  Got:      $DOWNLOADED_MD5"
        exit 1
    fi
fi

# Download tokens.txt
if [ -f "$SENSEVOICE_DIR/$SENSEVOICE_TOKENS" ]; then
    echo "  ✓ $SENSEVOICE_TOKENS already exists"
else
    echo "  Downloading $SENSEVOICE_TOKENS..."
    curl -L --progress-bar "$SENSEVOICE_TOKENS_URL" -o "$SENSEVOICE_DIR/$SENSEVOICE_TOKENS"
    echo -e "  ${GREEN}✓ Downloaded${NC}"
fi

# ============================================================================
# 2. Qwen3.5-0.8B-Q4_K_M GGUF (LLM)
# ============================================================================
QWEN_GGUF="Qwen3.5-0.8B-Q4_K_M.gguf"
# IMPORTANT: Replace this URL with your actual GitHub Release asset URL
# after uploading the GGUF file to a GitHub Release in this repository.
#
# Example: https://github.com/iMyth/QwenEcho/releases/download/v0.1.0/Qwen3.5-0.8B-Q4_K_M.gguf
QWEN_GGUF_URL="https://github.com/iMyth/QwenEcho/releases/latest/download/$QWEN_GGUF"
QWEN_GGUF_MD5="54e125d56fa41fe6eed66034f9fd4fa5"

echo ""
echo -e "${YELLOW}[2/2] Qwen3.5-0.8B-Q4_K_M GGUF (LLM)${NC}"
echo "  Source: GitHub Releases (this repository)"
echo ""

if [ -f "$MODELS_DIR/$QWEN_GGUF" ]; then
    echo "  ✓ $QWEN_GGUF already exists, verifying checksum..."
    EXISTING_MD5=$(md5 -q "$MODELS_DIR/$QWEN_GGUF" 2>/dev/null || md5sum "$MODELS_DIR/$QWEN_GGUF" | awk '{print $1}')
    if [ "$EXISTING_MD5" = "$QWEN_GGUF_MD5" ]; then
        echo -e "  ${GREEN}✓ Checksum matches${NC}"
    else
        echo -e "  ${RED}✗ Checksum mismatch (expected $QWEN_GGUF_MD5, got $EXISTING_MD5)${NC}"
        echo ""
        echo "  If you downloaded this model manually, the checksum may differ."
        echo "  To re-download from GitHub Releases, delete the file and re-run this script."
        echo "  Or download manually from HuggingFace:"
        echo "    https://huggingface.co/Qwen/Qwen3.5-0.8B-GGUF"
    fi
else
    echo "  Downloading $QWEN_GGUF (508MB)..."
    echo "  Note: This downloads from GitHub Releases. If the file doesn't exist there yet,"
    echo "        you'll need to upload it first. See README.md for instructions."
    echo ""

    if curl -L --progress-bar --fail "$QWEN_GGUF_URL" -o "$MODELS_DIR/$QWEN_GGUF" 2>/dev/null; then
        echo "  Verifying checksum..."
        DOWNLOADED_MD5=$(md5 -q "$MODELS_DIR/$QWEN_GGUF" 2>/dev/null || md5sum "$MODELS_DIR/$QWEN_GGUF" | awk '{print $1}')
        if [ "$DOWNLOADED_MD5" = "$QWEN_GGUF_MD5" ]; then
            echo -e "  ${GREEN}✓ Checksum matches${NC}"
        else
            echo -e "  ${YELLOW}⚠ Checksum mismatch (expected $QWEN_GGUF_MD5, got $DOWNLOADED_MD5)${NC}"
            echo "  The file may be from a different source or version."
            echo "  If you downloaded manually from HuggingFace, this is expected."
        fi
    else
        echo -e "  ${RED}✗ Download failed${NC}"
        echo ""
        echo "  The GGUF file is not yet uploaded to GitHub Releases."
        echo ""
        echo "  Manual download options:"
        echo "  1. HuggingFace (requires login):"
        echo "     https://huggingface.co/Qwen/Qwen3.5-0.8B-GGUF"
        echo ""
        echo "  2. After downloading, place it in:"
        echo "     $MODELS_DIR/$QWEN_GGUF"
        echo ""
        echo "  Then re-run this script to verify."
        exit 1
    fi
fi

# ============================================================================
# Summary
# ============================================================================
echo ""
echo "========================================="
echo -e " ${GREEN}✓ Model setup complete${NC}"
echo "========================================="
echo ""
echo "Models are now in: $MODELS_DIR/"
echo ""
echo "Next steps:"
echo "  1. flutter pub get"
echo "  2. flutter run"
echo ""
echo "The app will detect the models automatically on first launch."
echo ""
