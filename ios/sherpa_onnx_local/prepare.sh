#!/bin/bash
set -e

# Generate a combined sherpa-onnx.xcframework that statically links onnxruntime.
# This avoids the Xcode SPM bug where two binary targets both expose
# Headers/module.modulemap and collide in the build output directory.
#
# Usage: bash ios/sherpa_onnx_local/prepare.sh

PACKAGE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PACKAGE_DIR/../../build/ios"
ARTIFACTS_DIR="$BUILD_DIR/SourcePackages/artifacts/sherpa-onnx-spm"
OUTPUT_DIR="$PACKAGE_DIR/sherpa-onnx.xcframework"

VERSION="1.13.2"
RELEASE_URL="https://github.com/willwade/sherpa-onnx-spm/releases/download/$VERSION"

# Prefer already-resolved SPM artifacts; fall back to downloading.
if [ -d "$ARTIFACTS_DIR/sherpa-onnx/sherpa-onnx.xcframework" ] && \
   [ -d "$ARTIFACTS_DIR/onnxruntime/onnxruntime.xcframework" ]; then
    echo "Using existing SPM artifacts..."
    SHERPA_SRC="$ARTIFACTS_DIR/sherpa-onnx/sherpa-onnx.xcframework"
    ORT_SRC="$ARTIFACTS_DIR/onnxruntime/onnxruntime.xcframework"
else
    echo "Downloading XCFrameworks..."
    DOWNLOAD_DIR="$BUILD_DIR/sherpa-onnx-download"
    rm -rf "$DOWNLOAD_DIR"
    mkdir -p "$DOWNLOAD_DIR"
    cd "$DOWNLOAD_DIR"
    curl -L --fail -o sherpa-onnx.xcframework.zip "$RELEASE_URL/sherpa-onnx.xcframework.zip"
    curl -L --fail -o onnxruntime.xcframework.zip "$RELEASE_URL/onnxruntime.xcframework.zip"
    unzip -q sherpa-onnx.xcframework.zip
    unzip -q onnxruntime.xcframework.zip
    SHERPA_SRC="$DOWNLOAD_DIR/sherpa-onnx.xcframework"
    ORT_SRC="$DOWNLOAD_DIR/onnxruntime.xcframework"
fi

# Clean previous output.
rm -rf "$OUTPUT_DIR"

# Start from the sherpa-onnx XCFramework.
cp -R "$SHERPA_SRC" "$OUTPUT_DIR"

# Copy the onnxruntime static library into each sherpa-onnx slice so that
# combine-libs.sh can merge them into libsherpa-onnx.a.
for slice_dir in "$OUTPUT_DIR"/*/; do
    slice_name=$(basename "$slice_dir")
    [ "$slice_name" = "Headers" ] && continue

    ort_slice="$ORT_SRC/$slice_name"
    if [ ! -d "$ort_slice" ]; then
        echo "Skipping slice $slice_name: no matching onnxruntime slice"
        continue
    fi

    if [ -f "$ort_slice/onnxruntime.a" ]; then
        cp "$ort_slice/onnxruntime.a" "$slice_dir/"
    fi
    if [ -f "$ort_slice/libonnxruntime.a" ]; then
        cp "$ort_slice/libonnxruntime.a" "$slice_dir/"
    fi
done

# Combine libsherpa-onnx.a + onnxruntime.a into a single static library.
bash "$PACKAGE_DIR/combine-libs.sh" "$OUTPUT_DIR"

echo "Combined XCFramework created at: $OUTPUT_DIR"
