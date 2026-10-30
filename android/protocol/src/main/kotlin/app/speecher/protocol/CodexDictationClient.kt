package app.speecher.protocol

import java.util.Base64
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.jsonPrimitive

/** chatgpt.com's speech endpoints expect a browser, as the ChatGPT web app is their client. */
internal const val codexBrowserUserAgent =
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/144.0.0.0 Safari/537.36"

/** The spoken languages ChatGPT dictation listens for, as codes, Automatic first. */
val codexDictationLanguages: List<String> =
    listOf(AUTOMATIC_LANGUAGE) +
        ("af am ar az be bg bn bs ca cs cy da de el en es et fa fi fr gl gu he hi hr hu hy id is " +
                "it ja ka kk kn ko lt lv mi mk ml mn mr ms my ne nl no pl pt ro ru sk sl so sr sv sw " +
                "ta te th tl tr uk ur vi yue zh")
            .split(' ')

class CodexDictationClient(
    private val transport: WebSocketTransport,
    token: String,
    /** Sent as the session's `language`; Automatic leaves it out. */
    private val language: String,
    private val events: (SpeechEvent) -> Unit,
    endpoint: String = "wss://chatgpt.com/backend-api/dictation/stream",
) : WebSocketTransport.Listener, SpeechClient {
    private val stream =
        StreamLifecycle(
            transport,
            events,
            ::sendAudioMessage,
            {
                transport.sendText("{\"type\":\"audio.flush\",\"reason\":\"client\"}")
                transport.sendText("{\"type\":\"session.close\"}")
            },
            closeTimeoutSeconds = 8,
        )
    private val finalIds = mutableSetOf<String>()

    init {
        transport.open(
            endpoint,
            mapOf("User-Agent" to codexBrowserUserAgent),
            "chatgpt-dictation, openai-bearer.$token",
            this,
        )
        stream.failUnlessLiveWithin(10, "no session.start in 10s", retryable = true)
    }

    override fun onOpen() {
        if (stream.ended) return
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
            if (language != AUTOMATIC_LANGUAGE) put("language", JsonPrimitive(language))
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
        if (stream.ended) return
        val event = runCatching {
            Json.parseToJsonElement(text) as JsonObject
        }
            .getOrElse {
                stream.fail(ProviderFailureKind.Other, "bad event payload")
                return
            }
        when (event.string("type")) {
            "session.started" -> stream.goLive()
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
                    stream.complete()
                    transport.close(1000, null)
                }
            "transcript.failed" -> failWith(event)
            "session.error" -> if (event["fatal"]?.jsonPrimitive?.content == "true") failWith(event)
        }
    }

    override fun onFailure(error: Throwable, statusCode: Int?) = stream.onFailure(error, statusCode)

    override fun onClosed(code: Int, reason: String) = stream.onClosed(code, reason)

    /**
     * A provider error is retryable unless it is about sign-in or says `"retryable": false`. Its
     * code and type say what kind of failure it is.
     */
    private fun failWith(event: JsonObject) {
        val error = event["error"] as? JsonObject ?: JsonObject(emptyMap())
        val kind =
            if (event.authenticationError()) ProviderFailureKind.Authentication
            else streamedErrorKind(error)
        val retryable = error["retryable"]?.jsonPrimitive?.content
        stream.fail(
            kind,
            event.errorDetail(),
            kind != ProviderFailureKind.Authentication && retryable != "false",
        )
    }

    private fun JsonObject.errorDetail(): String {
        val error = this["error"] as? JsonObject ?: return "provider error"
        return "${error.string("code")} ${error.string("message")}".trim().take(120)
    }

    override fun sendAudio(pcm: ByteArray) = stream.sendAudio(pcm)

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
        stream.stop()
    }

    override fun cancel() = stream.cancel()
}

private fun JsonObject.string(key: String): String = this[key]?.jsonPrimitive?.content.orEmpty()

private fun JsonObject.authenticationError(): Boolean {
    val error = this["error"] as? JsonObject ?: return false
    return streamedErrorKind(error) == ProviderFailureKind.Authentication ||
        isAuthenticationError("${error.string("code")} ${error.string("message")}")
}
