#!/bin/bash
#
# Setup models for iOS Simulator debugging.
#
# This script copies models from the project's models/ directory to the
# currently booted iOS Simulator's app sandbox, so the app can start
# without downloading ~881MB from the network.
#
# Prerequisites:
#   1. Run ./scripts/setup_models.sh first to download models to models/
#   2. Build and run the app at least once on the simulator (to create the sandbox)
#   3. Ensure the simulator is booted (running)
#
# Usage:
#   bash scripts/setup_simulator_models.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
MODELS_DIR="$PROJECT_ROOT/models"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo "========================================="
echo " iOS Simulator Model Setup"
echo "========================================="
echo ""

# Check if models exist locally
if [ ! -d "$MODELS_DIR/SenseVoiceSmall-onnx" ] || [ ! -f "$MODELS_DIR/Qwen3.5-0.8B-Q4_K_M.gguf" ]; then
    echo -e "${RED}✗ Models not found in $MODELS_DIR${NC}"
    echo ""
    echo "Please run first:"
    echo "  bash scripts/setup_models.sh"
    exit 1
fi

echo -e "${GREEN}✓ Local models found${NC}"
echo ""

# Find booted simulator
BOOTED=$(xcrun simctl list devices booted | grep -E "iPhone|iPad" | head -1)
if [ -z "$BOOTED" ]; then
    echo -e "${RED}✗ No booted iOS Simulator found${NC}"
    echo ""
    echo "Please:"
    echo "  1. Open Xcode or run 'open -a Simulator'"
    echo "  2. Boot an iOS simulator"
    echo "  3. Build and run the app at least once"
    echo "  4. Re-run this script"
    exit 1
fi

echo "Booted simulator:"
echo "  $BOOTED"
echo ""

# Get simulator UDID
SIM_UDID=$(xcrun simctl list devices booted | grep -E "iPhone|iPad" | head -1 | sed 's/.*(\(.*\)).*/\1/')
echo "Simulator UDID: $SIM_UDID"

# Get app container path
APP_CONTAINER=$(xcrun simctl get_app_container booted com.myth.qwenecho data 2>/dev/null || echo "")
if [ -z "$APP_CONTAINER" ]; then
    echo -e "${RED}✗ App not installed on simulator${NC}"
    echo ""
    echo "Please build and run the app on the simulator first:"
    echo "  flutter run"
    echo ""
    echo "Then re-run this script."
    exit 1
fi

echo "App container: $APP_CONTAINER"
echo ""

# Copy models
TARGET_DIR="$APP_CONTAINER/Library/Application Support/models"
mkdir -p "$TARGET_DIR"

echo "Copying ASR model (SenseVoiceSmall-onnx)..."
cp -r "$MODELS_DIR/SenseVoiceSmall-onnx" "$TARGET_DIR/"

echo "Copying LLM model (Qwen3.5-0.8B-Q4_K_M.gguf)..."
cp "$MODELS_DIR/Qwen3.5-0.8B-Q4_K_M.gguf" "$TARGET_DIR/"

echo ""
echo -e "${GREEN}✓ Models copied successfully${NC}"
echo ""
echo "Target directory: $TARGET_DIR"
echo ""
echo "Next steps:"
echo "  1. Kill the app on simulator (swipe up or Cmd+H)"
echo "  2. Relaunch the app"
echo "  3. The download screen should be skipped"
echo ""

# List what we copied
echo "Copied files:"
ls -lh "$TARGET_DIR/"
echo ""
echo "ASR model contents:"
ls -lh "$TARGET_DIR/SenseVoiceSmall-onnx/" | head -5
