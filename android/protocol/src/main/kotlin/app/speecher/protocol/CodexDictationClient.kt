package app.speecher.protocol

import java.util.Base64
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.jsonPrimitive
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener

class CodexDictationClient(
    http: OkHttpClient,
    token: String,
    private val events: (SpeechEvent) -> Unit,
    endpoint: String = "wss://chatgpt.com/backend-api/dictation/stream",
) : WebSocketListener(), SpeechClient {
    private val lock = Any()
    private val pending = mutableListOf<ByteArray>()
    private val finalIds = mutableSetOf<String>()
    private var socket: WebSocket? = null
    @Volatile private var started = false
    @Volatile private var stopped = false
    @Volatile private var cancelled = false
    @Volatile private var completed = false
    @Volatile private var failed = false
    private val deadline = Executors.newSingleThreadScheduledExecutor { task ->
        Thread(task, "codex-dictation-deadline").apply { isDaemon = true }
    }

    init {
        val request =
            Request.Builder()
                .url(endpoint)
                .header("Sec-WebSocket-Protocol", "chatgpt-dictation, openai-bearer.$token")
                .header(
                    "User-Agent",
                    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/144.0.0.0 Safari/537.36",
                )
                .build()
        socket = http.newWebSocket(request, this)
        deadline.schedule({ if (!started) fail(false) }, 10, TimeUnit.SECONDS)
    }

    override fun onOpen(webSocket: WebSocket, response: Response) {
        if (cancelled || failed) return
        socket = webSocket
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
        webSocket.send(
            buildJsonObject {
                put("type", JsonPrimitive("session.start"))
                put("config", config)
            }
                .toString()
        )
    }

    override fun onMessage(webSocket: WebSocket, text: String) {
        if (cancelled || completed || failed) return
        val event = runCatching {
            Json.parseToJsonElement(text) as JsonObject
        }
            .getOrElse {
                fail(false)
                return
            }
        when (event.string("type")) {
            "session.started" -> {
                if (started) return
                val buffered =
                    synchronized(lock) {
                        started = true
                        pending.toList().also { pending.clear() }
                    }
                buffered.forEach(::sendAudioMessage)
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
                    webSocket.close(1000, null)
                }
            "transcript.failed" -> fail(event.authenticationError())
            "session.error" ->
                if (event["fatal"]?.jsonPrimitive?.content == "true") {
                    fail(event.authenticationError())
                }
        }
    }

    override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
        fail(response?.code == 401 || response?.code == 403)
    }

    override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
        fail(false)
    }

    override fun sendAudio(pcm: ByteArray) {
        if (stopped || cancelled || failed || pcm.isEmpty()) return
        val direct =
            synchronized(lock) {
                if (started) true
                else {
                    pending.add(pcm)
                    false
                }
            }
        if (direct) sendAudioMessage(pcm)
    }

    private fun sendAudioMessage(pcm: ByteArray) {
        socket?.send(
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
        socket?.send("{\"type\":\"audio.flush\",\"reason\":\"client\"}")
        socket?.send("{\"type\":\"session.close\"}")
        deadline.schedule({ if (!completed) fail(false) }, 8, TimeUnit.SECONDS)
    }

    override fun cancel() {
        cancelled = true
        deadline.shutdownNow()
        synchronized(lock) { pending.clear() }
        socket?.cancel()
    }

    private fun fail(authentication: Boolean) {
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
        events(SpeechEvent.Failed(authentication))
        socket?.cancel()
    }
}

private fun JsonObject.string(key: String): String = this[key]?.jsonPrimitive?.content.orEmpty()

private fun JsonObject.authenticationError(): Boolean {
    val error = this["error"] as? JsonObject
    val detail =
        "${error?.string("code").orEmpty()} ${error?.string("message").orEmpty()}".lowercase()
    return listOf("401", "403", "unauthorized", "forbidden").any(detail::contains)
}
