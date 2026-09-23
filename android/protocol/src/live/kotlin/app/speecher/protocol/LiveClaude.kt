package app.speecher.protocol

import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

/** Opt-in live Claude BC TLS check. Set CLAUDE_AUTH to a cliproxy claude oauth json path. */
fun main() {
    val path =
        System.getenv("CLAUDE_AUTH")
            ?: error("set CLAUDE_AUTH to a cliproxy claude oauth json path")
    val token =
        Json.parseToJsonElement(Files.readString(Path.of(path)))
            .jsonObject
            .getValue("access_token")
            .jsonPrimitive
            .content
    val results = LinkedBlockingQueue<SpeechEvent>()
    val fingerprint = TlsFingerprints.active
    println("TLS profile: ${fingerprint.name}")
    val transport = BcTlsWebSocketTransport(fingerprint)
    val client = ClaudeVoiceClient(transport, token, emptyList(), { results.add(it) })
    try {
        val event = results.poll(15, TimeUnit.SECONDS)
        check(event == SpeechEvent.Connected) { "Live Claude verification failed: $event" }
        println("HTTP 101; Claude voice stream connected over BC TLS")
    } finally {
        client.cancel()
    }
}
