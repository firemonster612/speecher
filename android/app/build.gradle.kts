plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.ktfmt)
}

android {
    namespace = "app.speecher.android"
    compileSdk = 37

    defaultConfig {
        applicationId = "app.speecher.android"
        minSdk = 31
        targetSdk = 37
        versionCode = 1
        versionName = "0.1.0"
    }

    buildFeatures {
        compose = true
        buildConfig = true
    }

    buildTypes {
        // Debug builds can point speech at a local fake server for emulator tests:
        // ./gradlew assembleDebug -PfakeSpeech=http://10.0.2.2:8765
        debug {
            buildConfigField(
                "String",
                "FAKE_SPEECH_BASE",
                "\"${providers.gradleProperty("fakeSpeech").getOrElse("")}\"",
            )
        }
        release { buildConfigField("String", "FAKE_SPEECH_BASE", "\"\"") }
    }
    buildFeatures { buildConfig = true }
    sourceSets.named("main") { res.directories.add("src/engine/res") }

    lint {
        warningsAsErrors = true
        abortOnError = true
    }
}

kotlin {
    jvmToolchain(21)
    compilerOptions { allWarningsAsErrors = true }
}

ktfmt { kotlinLangStyle() }

dependencies {
    implementation(project(":protocol"))
    implementation(platform(libs.compose.bom))
    implementation(libs.activity.compose)
    implementation(libs.compose.ui)
    implementation(libs.compose.material3)
    implementation(libs.compose.ui.tooling.preview)
    debugImplementation(libs.compose.ui.tooling)
    implementation(libs.browser)
    implementation(libs.okhttp)
    implementation(libs.lifecycle)
    implementation(libs.savedstate)
    implementation(libs.serialization.json)
    testImplementation(libs.junit4)
    testImplementation(libs.mockwebserver)
}
