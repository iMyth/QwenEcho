#!/bin/bash
set -e

# Combine sherpa-onnx and onnxruntime libraries for each platform slice
XCFW_PATH="$1"

if [[ ! -d "$XCFW_PATH" ]]; then
    echo "Usage: $0 <xcframework_path>"
    exit 1
fi

echo "=== Combining libraries in $XCFW_PATH ==="

for slice_dir in "$XCFW_PATH"/*/; do
    slice_name="$(basename "$slice_dir")"
    
    if [[ "$slice_name" == "Headers" ]]; then
        continue
    fi
    
    echo "Processing slice: $slice_name"
    
    # Check if both libraries exist
    if [[ -f "$slice_dir/libsherpa-onnx.a" && -f "$slice_dir/onnxruntime.a" ]]; then
        echo "  Combining libsherpa-onnx.a + onnxruntime.a -> libsherpa-onnx.a"
        
        # Use libtool to combine the static libraries
        libtool -static -o "$slice_dir/libsherpa-onnx.combined.a" \
            "$slice_dir/libsherpa-onnx.a" \
            "$slice_dir/onnxruntime.a"
        
        # Replace the original with combined library
        mv "$slice_dir/libsherpa-onnx.combined.a" "$slice_dir/libsherpa-onnx.a"
        
        # Remove the separate onnxruntime library
        rm "$slice_dir/onnxruntime.a"
        rm -f "$slice_dir/libonnxruntime.a"
        
        echo "  Combined library size: $(du -h "$slice_dir/libsherpa-onnx.a" | cut -f1)"
    fi
done

echo "=== Library combination complete ==="
