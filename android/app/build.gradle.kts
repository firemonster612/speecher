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
        versionCode = 15
        versionName = "0.1.14"
    }

    buildFeatures {
        compose = true
        buildConfig = true
    }

    packaging { resources { excludes += "META-INF/{LICENSE.md,NOTICE.md,versions/**}" } }

    // The release key lives outside the repo; see docs/android/releasing.md.
    val releaseKey = file("${System.getProperty("user.home")}/.config/speecher-android/release.jks")
    signingConfigs {
        if (releaseKey.exists()) {
            create("release") {
                storeFile = releaseKey
                storePassword = releaseKey.resolveSibling("release.password").readText()
                keyAlias = "speecher"
                keyPassword = storePassword
            }
        }
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
        release {
            buildConfigField("String", "FAKE_SPEECH_BASE", "\"\"")
            signingConfig = signingConfigs.findByName("release")
        }
    }
    sourceSets.named("main") { res.directories.add("src/engine/res") }

    lint {
        warningsAsErrors = true
        abortOnError = true
        // lint.xml carries the only exemptions, all scoped to BouncyCastle's bctls jar: its
        // bytecode
        // trips TrustAllX509TrustManager (a false positive; our TLS code validates properly) and it
        // is pinned at 1.86. Every other dependency and every other check stays strict. See
        // AGENTS.md.
        lintConfig = file("lint.xml")
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
    implementation(libs.okhttp.brotli)
    implementation(libs.lifecycle)
    implementation(libs.savedstate)
    implementation(libs.serialization.json)
    testImplementation(libs.junit4)
    testImplementation(libs.mockwebserver)
    testImplementation(libs.robolectric)
}
