package app.speecher.protocol

import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

fun main() {
    val auth =
        Json.parseToJsonElement(
                Files.readString(Path.of(System.getProperty("user.home"), ".codex/auth.json"))
            )
            .jsonObject
    val token = auth.getValue("tokens").jsonObject.getValue("access_token").jsonPrimitive.content
    val results = LinkedBlockingQueue<SpeechEvent>()
    val fingerprint = TlsFingerprints.active
    println("TLS profile: ${fingerprint.name}")
    val transport =
        BcTlsWebSocketTransport(fingerprint) { bytes ->
            println("ClientHello: " + bytes.joinToString("") { "%02x".format(it) })
        }
    val client = CodexDictationClient(transport, token, { results.add(it) })
    try {
        val event = results.poll(15, TimeUnit.SECONDS)
        check(event == SpeechEvent.Connected) { "Live verification failed: $event" }
        println("HTTP 101; received session.started over BC TLS")
    } finally {
        client.cancel()
    }
}
