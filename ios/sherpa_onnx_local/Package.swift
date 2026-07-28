// swift-tools-version: 5.9
//
// Local SPM package wrapping a combined sherpa-onnx.xcframework.
//
// This package exists to sidestep an Xcode SPM bug: when two separate binary
// targets (`sherpa-onnx` and `onnxruntime`) both ship their own
// Headers/module.modulemap, Xcode's build system collides them in the
// derived-data module cache and the build fails with "duplicate module" errors.
//
// `prepare.sh` downloads both upstream XCFrameworks and merges the onnxruntime
// static library into the sherpa-onnx slices, producing a single combined
// `sherpa-onnx.xcframework` that this Package.swift references.
//
// Run once before the first Xcode build:
//     bash ios/sherpa_onnx_local/prepare.sh

import PackageDescription

let package = Package(
    name: "sherpa_onnx_local",
    platforms: [.iOS(.v16)],
    products: [
        .library(
            name: "SherpaOnnx",
            targets: ["SherpaOnnx"]
        ),
    ],
    targets: [
        // Swift API wrapper — lives in Sources/SherpaOnnx/SherpaOnnx.swift
        // and does `@_exported import sherpa_onnx` to re-export the C types.
        .target(
            name: "SherpaOnnx",
            dependencies: ["sherpa-onnx"],
            path: "Sources/SherpaOnnx",
            linkerSettings: [
                .linkedLibrary("c++"),
                .linkedFramework("Accelerate"),
                .linkedFramework("AVFoundation"),
            ]
        ),
        // Combined XCFramework produced by `prepare.sh`.
        .binaryTarget(
            name: "sherpa-onnx",
            path: "sherpa-onnx.xcframework"
        ),
    ]
)
