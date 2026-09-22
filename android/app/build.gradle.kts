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

    buildFeatures { compose = true }

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
    implementation(libs.browser)
    implementation(libs.okhttp)
}
