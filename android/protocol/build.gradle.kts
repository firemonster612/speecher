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
    implementation(libs.bouncycastle.tls)
    implementation(libs.bouncycastle.prov)
    testImplementation(libs.junit.jupiter)
    testImplementation(libs.mockwebserver)
    testRuntimeOnly(libs.junit.platform.launcher)
}

tasks.test { useJUnitPlatform() }

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
