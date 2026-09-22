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
    testImplementation(libs.junit.jupiter)
    testRuntimeOnly(libs.junit.platform.launcher)
}

tasks.test { useJUnitPlatform() }
