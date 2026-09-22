plugins {
    alias(libs.plugins.kotlin.jvm)
    alias(libs.plugins.ktfmt)
}

kotlin {
    jvmToolchain(21)
    compilerOptions { allWarningsAsErrors = true }
}

ktfmt { kotlinLangStyle() }

dependencies {
    implementation(libs.serialization.json)
    implementation(libs.okhttp)
    testImplementation(libs.junit.jupiter)
    testImplementation(libs.mockwebserver)
    testRuntimeOnly(libs.junit.platform.launcher)
}

tasks.test { useJUnitPlatform() }
