package app.speecher.protocol

import java.util.Base64
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.jsonPrimitive

/** chatgpt.com's speech endpoints expect a browser, as the ChatGPT web app is their client. */
internal const val codexBrowserUserAgent =
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/144.0.0.0 Safari/537.36"

class CodexDictationClient(
    private val transport: WebSocketTransport,
    token: String,
    private val events: (SpeechEvent) -> Unit,
    endpoint: String = "wss://chatgpt.com/backend-api/dictation/stream",
) : WebSocketTransport.Listener, SpeechClient {
    private val lock = Any()
    private val pending = mutableListOf<ByteArray>()
    private val finalIds = mutableSetOf<String>()
    @Volatile private var started = false
    @Volatile private var stopped = false
    @Volatile private var cancelled = false
    @Volatile private var completed = false
    @Volatile private var failed = false
    private val deadline = Executors.newSingleThreadScheduledExecutor { task ->
        Thread(task, "codex-dictation-deadline").apply { isDaemon = true }
    }

    init {
        transport.open(
            endpoint,
            mapOf("User-Agent" to codexBrowserUserAgent),
            "chatgpt-dictation, openai-bearer.$token",
            this,
        )
        deadline.schedule(
            { if (!started) fail(false, "no session.start in 10s") },
            10,
            TimeUnit.SECONDS,
        )
    }

    override fun onOpen() {
        if (cancelled || failed) return
        val vad = buildJsonObject {
            put("type", JsonPrimitive("server_vad"))
            put("threshold", JsonPrimitive(0.5))
            put("prefix_padding_ms", JsonPrimitive(300))
            put("silence_duration_ms", JsonPrimitive(500))
        }
        val config = buildJsonObject {
            put("input_audio_format", JsonPrimitive("pcm16"))
            put("sample_rate_hz", JsonPrimitive(16000))
            put("num_channels", JsonPrimitive(1))
            put("max_buffer_size_bytes", JsonPrimitive(4 * 1024 * 1024))
            put("max_utterance_duration_ms", JsonPrimitive(30000))
            put("session_ttl_ms", JsonPrimitive(300000))
            put("provider_mode", JsonPrimitive("streaming_sse"))
            put("transcript_delivery_mode", JsonPrimitive("segment"))
            put("vad", vad)
        }
        transport.sendText(
            buildJsonObject {
                put("type", JsonPrimitive("session.start"))
                put("config", config)
            }
                .toString()
        )
    }

    override fun onText(text: String) {
        if (cancelled || completed || failed) return
        val event = runCatching {
            Json.parseToJsonElement(text) as JsonObject
        }
            .getOrElse {
                fail(false, "bad event payload")
                return
            }
        when (event.string("type")) {
            "session.started" -> {
                if (started) return
                // Flushed under the lock so audio captured meanwhile queues behind it, not ahead.
                synchronized(lock) {
                    started = true
                    pending.forEach(::sendAudioMessage)
                    pending.clear()
                }
                if (stopped) closeSession() else events(SpeechEvent.Connected)
            }
            "transcript.segment",
            "transcript.final" -> {
                val id = event.string("utterance_id")
                if (id.isNotEmpty() && id in finalIds) return
                val value = event.string("text").trim()
                if (value.isNotEmpty()) {
                    if (event.string("type") == "transcript.final") {
                        if (id.isNotEmpty()) finalIds.add(id)
                        events(SpeechEvent.Final(value))
                    } else events(SpeechEvent.Partial(value))
                }
            }
            "session.updated" ->
                if ((event["session"] as? JsonObject)?.string("status") == "closed") {
                    completed = true
                    deadline.shutdownNow()
                    events(SpeechEvent.Completed)
                    transport.close(1000, null)
                }
            "transcript.failed" -> failWith(event)
            "session.error" -> if (event["fatal"]?.jsonPrimitive?.content == "true") failWith(event)
        }
    }

    override fun onFailure(error: Throwable, statusCode: Int?) {
        val code = statusCode
        val detail =
            listOfNotNull(code?.let { "HTTP $it" }, error.message?.takeIf { it.isNotBlank() })
                .joinToString(": ")
                .ifEmpty { "connect failed" }
        val authentication = code == 401 || code == 403
        fail(authentication, detail, retryable = !authentication)
    }

    override fun onClosed(code: Int, reason: String) {
        fail(false, "closed $code ${reason.take(80)}".trim(), isRetryableClose(code))
    }

    /** A provider error is retryable unless it is about sign-in or says `"retryable": false`. */
    private fun failWith(event: JsonObject) {
        val authentication = event.authenticationError()
        val retryable = (event["error"] as? JsonObject)?.get("retryable")?.jsonPrimitive?.content
        fail(authentication, event.errorDetail(), !authentication && retryable != "false")
    }

    private fun JsonObject.errorDetail(): String {
        val error = this["error"] as? JsonObject ?: return "provider error"
        return "${error.string("code")} ${error.string("message")}".trim().take(120)
    }

    override fun sendAudio(pcm: ByteArray) {
        if (stopped || cancelled || failed || pcm.isEmpty()) return
        synchronized(lock) { if (started) sendAudioMessage(pcm) else pending.add(pcm) }
    }

    private fun sendAudioMessage(pcm: ByteArray) {
        transport.sendText(
            buildJsonObject {
                put("type", JsonPrimitive("audio.append"))
                put("audio", JsonPrimitive(Base64.getEncoder().encodeToString(pcm)))
            }
                .toString()
        )
    }

    override fun stop() {
        if (stopped || cancelled) return
        stopped = true
        if (started) closeSession()
    }

    private fun closeSession() {
        transport.sendText("{\"type\":\"audio.flush\",\"reason\":\"client\"}")
        transport.sendText("{\"type\":\"session.close\"}")
        deadline.schedule({ if (!completed) fail(false, "no close in 8s") }, 8, TimeUnit.SECONDS)
    }

    override fun cancel() {
        cancelled = true
        deadline.shutdownNow()
        synchronized(lock) { pending.clear() }
        transport.cancel()
    }

    private fun fail(authentication: Boolean, detail: String = "", retryable: Boolean = false) {
        val first =
            synchronized(lock) {
                if (cancelled || completed || failed) false
                else {
                    failed = true
                    true
                }
            }
        if (!first) return
        deadline.shutdownNow()
        events(SpeechEvent.Failed(authentication, detail, retryable))
        transport.cancel()
    }
}

private fun JsonObject.string(key: String): String = this[key]?.jsonPrimitive?.content.orEmpty()

private fun JsonObject.authenticationError(): Boolean {
    val error = this["error"] as? JsonObject
    val detail =
        "${error?.string("code").orEmpty()} ${error?.string("message").orEmpty()}".lowercase()
    return isAuthenticationError(detail)
}
