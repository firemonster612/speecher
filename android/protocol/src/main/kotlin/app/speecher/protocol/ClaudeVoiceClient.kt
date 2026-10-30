package app.speecher.protocol

import okhttp3.HttpUrl.Companion.toHttpUrl

class ClaudeVoiceClient(
    private val transport: WebSocketTransport,
    token: String,
    vocabulary: List<String>,
    language: String,
    private val events: (SpeechEvent) -> Unit,
    endpoint: String = "wss://claude.ai/api/ws/speech_to_text/voice_stream",
) : WebSocketTransport.Listener, SpeechClient {
    private val stream =
        StreamLifecycle(
            transport,
            events,
            { transport.sendBinary(it) },
            { transport.sendText("{\"type\":\"CloseStream\"}") },
            closeTimeoutSeconds = 5,
        )
    private var lastInterim = ""

    init {
        val httpEndpoint =
            endpoint
                .replaceFirst(Regex("^wss://"), "https://")
                .replaceFirst(Regex("^ws://"), "http://")
        val url =
            httpEndpoint
                .toHttpUrl()
                .newBuilder()
                .apply {
                    claudeVoiceStreamQuery(language).forEach { (key, value) ->
                        addQueryParameter(key, value)
                    }
                }
                .build()
        val headers =
            mutableMapOf(
                "Authorization" to "Bearer $token",
                "User-Agent" to "Claude-Code",
                "x-app" to "cli",
                "anthropic-client-platform" to "linux",
            )
        claudeVoiceKeytermsHeader(vocabulary).takeIf(String::isNotEmpty)?.let {
            headers["x-config-keyterms"] = it
        }
        transport.open(url.toString(), headers, null, this)
        stream.failUnlessLiveWithin(10, "no connect in 10s", retryable = true)
    }

    override fun onOpen() {
        if (stream.ended) return
        transport.sendText("{\"type\":\"KeepAlive\"}")
        stream.repeat(8) { transport.sendText("{\"type\":\"KeepAlive\"}") }
        stream.goLive()
    }

    override fun onText(text: String) {
        if (stream.ended) return
        when (val event = parseClaudeVoiceEvent(text)) {
            is ClaudeVoiceEvent.Working -> {
                val value = event.text.trim().replace(Regex("\\s+"), " ")
                if (value.isNotEmpty()) {
                    lastInterim = value
                    events(SpeechEvent.Partial(value))
                }
            }
            is ClaudeVoiceEvent.Endpoint -> {
                val value = event.text.trim().replace(Regex("\\s+"), " ").ifEmpty { lastInterim }
                if (value.isNotEmpty()) events(SpeechEvent.Final(value))
                lastInterim = ""
                if (stream.stopped) {
                    stream.complete()
                    transport.close(1000, null)
                }
            }
            is ClaudeVoiceEvent.ServerError -> {
                val authentication = isAuthenticationError(event.summary)
                stream.fail(
                    if (authentication) ProviderFailureKind.Authentication
                    else ProviderFailureKind.Server,
                    event.summary.take(120),
                    retryable = !authentication,
                )
            }
            is ClaudeVoiceEvent.TranscriptError ->
                stream.fail(ProviderFailureKind.Server, event.summary.take(120), retryable = true)
            ClaudeVoiceEvent.Unknown -> Unit
        }
    }

    override fun onFailure(error: Throwable, statusCode: Int?) = stream.onFailure(error, statusCode)

    override fun onClosed(code: Int, reason: String) = stream.onClosed(code, reason)

    override fun sendAudio(pcm: ByteArray) = stream.sendAudio(pcm)

    override fun stop() {
        if (stream.stop() && !stream.live) stream.failUnlessLiveWithin(5, "no connect in 5s")
    }

    override fun cancel() = stream.cancel()
}
