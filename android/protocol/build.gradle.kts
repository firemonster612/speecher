plugins {
    alias(libs.plugins.kotlin.jvm)
    alias(libs.plugins.kotlin.serialization)
    alias(libs.plugins.ktfmt)
    alias(libs.plugins.animalsniffer)
}

kotlin {
    jvmToolchain(21)
    compilerOptions { allWarningsAsErrors = true }
}

ktfmt { kotlinLangStyle() }

dependencies {
    implementation(libs.serialization.json)
    implementation(libs.okhttp)
    implementation(libs.bouncycastle.tls)
    implementation(libs.bouncycastle.prov)
    // Android 12's API, the app's minSdk: check fails on a JDK call the phone lacks.
    signature(variantOf(libs.android.api.signature) { artifactType("signature") })
    testImplementation(libs.junit.jupiter)
    testImplementation(libs.mockwebserver)
    testImplementation(libs.okhttp.tls)
    testRuntimeOnly(libs.junit.platform.launcher)
}

tasks.test { useJUnitPlatform() }

// Only the shipped code runs on the phone; tests and live checks run on the host JVM.
animalsniffer { sourceSets = listOf(project.sourceSets.main.get()) }

val live = sourceSets.create("live")

configurations[live.implementationConfigurationName].extendsFrom(
    configurations.implementation.get()
)

live.compileClasspath += sourceSets.main.get().output

live.runtimeClasspath += sourceSets.main.get().output

tasks.register<JavaExec>("liveDictation") {
    description = "Opt-in live BC TLS verification using ~/.codex/auth.json"
    classpath = live.runtimeClasspath
    mainClass.set("app.speecher.protocol.LiveDictationKt")
}

tasks.register<JavaExec>("liveClaude") {
    description = "Opt-in live Claude BC TLS verification; set CLAUDE_AUTH to token json"
    classpath = live.runtimeClasspath
    mainClass.set("app.speecher.protocol.LiveClaudeKt")
}

tasks.register<JavaExec>("liveTranscribe") {
    description =
        "Opt-in live ChatGPT transcribe + refinement over BC TLS HTTP using ~/.codex/auth.json"
    classpath = live.runtimeClasspath
    mainClass.set("app.speecher.protocol.LiveTranscribeKt")
}
