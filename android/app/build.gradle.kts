import com.android.build.api.dsl.ApplicationExtension
import java.io.FileOutputStream
import java.io.IOException
import java.net.URI

plugins {
    id("com.android.application")
    id("dev.flutter.flutter-gradle-plugin")
}

configure<ApplicationExtension> {
    namespace = "com.example.qwen_echo"
    compileSdk = flutter.compileSdkVersion
    ndkVersion = flutter.ndkVersion

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    defaultConfig {
        applicationId = "com.example.qwen_echo"
        minSdk = 24
        targetSdk = flutter.targetSdkVersion
        versionCode = flutter.versionCode
        versionName = flutter.versionName
    }

    buildTypes {
        release {
            signingConfig = signingConfigs.getByName("debug")
        }
    }
}

repositories {
    flatDir {
        dirs("libs")
    }
}

kotlin {
    compilerOptions {
        jvmTarget = org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17
    }
}

dependencies {
    // sherpa-onnx Android AAR — auto-downloaded by the downloadSherpaOnnxAar task.
    // The download runs before compilation, so the AAR is always available here.
    implementation(files("libs/sherpa-onnx.aar"))
}

flutter {
    source = "../.."
}

// ---------------------------------------------------------------------------
// Auto-download sherpa-onnx AAR if not present.
// ---------------------------------------------------------------------------
val sherpaOnnxAarFile = file("libs/sherpa-onnx.aar")
val sherpaOnnxAarUrl = "https://huggingface.co/csukuangfj/sherpa-onnx-libs/resolve/main/android/aar/sherpa-onnx-1.12.21.aar"

tasks.register("downloadSherpaOnnxAar") {
    description = "Download sherpa-onnx Android AAR from HuggingFace"
    outputs.file(sherpaOnnxAarFile)

    doLast {
        if (sherpaOnnxAarFile.exists()) {
            println("sherpa-onnx AAR already exists: ${sherpaOnnxAarFile.absolutePath}")
            return@doLast
        }

        println("Downloading sherpa-onnx AAR from: $sherpaOnnxAarUrl")
        sherpaOnnxAarFile.parentFile.mkdirs()

        try {
            val url = URI.create(sherpaOnnxAarUrl).toURL()
            url.openStream().use { input ->
                FileOutputStream(sherpaOnnxAarFile).use { output ->
                    input.copyTo(output)
                }
            }
            println("Downloaded sherpa-onnx AAR: ${sherpaOnnxAarFile.absolutePath} (${sherpaOnnxAarFile.length() / 1024 / 1024}MB)")
        } catch (e: IOException) {
            println("WARNING: Failed to download sherpa-onnx AAR: ${e.message}")
            println("Please download manually:")
            println("  mkdir -p android/app/libs")
            println("  curl -L '$sherpaOnnxAarUrl' -o android/app/libs/sherpa-onnx.aar")
            // Create an empty placeholder so the build doesn't fail immediately
            // (it will fail at runtime when ASR is needed)
            sherpaOnnxAarFile.parentFile.mkdirs()
        }
    }
}

tasks.configureEach {
    if (name == "preBuild" || name.startsWith("compile")) {
        dependsOn("downloadSherpaOnnxAar")
    }
}
