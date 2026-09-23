package app.speecher.protocol

import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import okhttp3.HttpUrl.Companion.toHttpUrl

class ClaudeVoiceClient(
    private val transport: WebSocketTransport,
    token: String,
    vocabulary: List<String>,
    private val events: (SpeechEvent) -> Unit,
    endpoint: String = "wss://claude.ai/api/ws/speech_to_text/voice_stream",
) : WebSocketTransport.Listener, SpeechClient {
    private val lock = Any()
    private val pending = mutableListOf<ByteArray>()
    private var pendingBytes = 0
    @Volatile private var connected = false
    @Volatile private var stopped = false
    @Volatile private var cancelled = false
    @Volatile private var completed = false
    @Volatile private var failed = false
    private var lastInterim = ""
    private val keepAlive = Executors.newSingleThreadScheduledExecutor { task ->
        Thread(task, "claude-voice-keepalive").apply { isDaemon = true }
    }

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
                    claudeVoiceStreamQuery().forEach { (key, value) ->
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
        keepAlive.schedule(
            { if (!connected) fail(false, "no connect in 10s") },
            10,
            TimeUnit.SECONDS,
        )
    }

    override fun onOpen() {
        if (cancelled || failed) return
        val buffered =
            synchronized(lock) {
                connected = true
                pending.toList().also {
                    pending.clear()
                    pendingBytes = 0
                }
            }
        transport.sendText("{\"type\":\"KeepAlive\"}")
        keepAlive.scheduleAtFixedRate(
            { if (!cancelled && !completed) transport.sendText("{\"type\":\"KeepAlive\"}") },
            8,
            8,
            TimeUnit.SECONDS,
        )
        buffered.forEach { transport.sendBinary(it) }
        if (stopped) closeStream() else events(SpeechEvent.Connected)
    }

    override fun onText(text: String) {
        if (cancelled || completed || failed) return
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
                if (stopped) {
                    completed = true
                    keepAlive.shutdownNow()
                    events(SpeechEvent.Completed)
                    transport.close(1000, null)
                }
            }
            is ClaudeVoiceEvent.ServerError ->
                fail(isAuthenticationError(event.summary), event.summary.take(120))
            is ClaudeVoiceEvent.TranscriptError -> fail(false, event.summary.take(120))
            ClaudeVoiceEvent.Unknown -> Unit
        }
    }

    override fun onFailure(error: Throwable, statusCode: Int?) {
        val code = statusCode
        val detail =
            listOfNotNull(code?.let { "HTTP $it" }, error.message?.takeIf { it.isNotBlank() })
                .joinToString(": ")
                .ifEmpty { "connect failed" }
        fail(code == 401 || code == 403, detail)
    }

    override fun onClosed(code: Int, reason: String) {
        fail(false, "closed $code ${reason.take(80)}".trim())
    }

    override fun sendAudio(pcm: ByteArray) {
        if (stopped || cancelled || failed || pcm.isEmpty()) return
        var overflow = false
        val direct =
            synchronized(lock) {
                if (connected) true
                else if (pendingBytes + pcm.size > 4 * 1024 * 1024) {
                    overflow = true
                    false
                } else {
                    pending.add(pcm)
                    pendingBytes += pcm.size
                    false
                }
            }
        if (overflow) {
            fail(false, "audio buffer overflow")
            return
        }
        if (direct) transport.sendBinary(pcm)
    }

    override fun stop() {
        if (stopped || cancelled) return
        stopped = true
        if (connected) closeStream()
        else
            keepAlive.schedule(
                { if (!connected) fail(false, "no connect in 5s") },
                5,
                TimeUnit.SECONDS,
            )
    }

    private fun closeStream() {
        transport.sendText("{\"type\":\"CloseStream\"}")
        keepAlive.schedule({ if (!completed) fail(false, "no close in 5s") }, 5, TimeUnit.SECONDS)
    }

    override fun cancel() {
        cancelled = true
        keepAlive.shutdownNow()
        synchronized(lock) {
            pending.clear()
            pendingBytes = 0
        }
        transport.cancel()
    }

    private fun fail(authentication: Boolean, detail: String = "") {
        val first =
            synchronized(lock) {
                if (cancelled || completed || failed) false
                else {
                    failed = true
                    true
                }
            }
        if (!first) return
        keepAlive.shutdownNow()
        events(SpeechEvent.Failed(authentication, detail))
        transport.cancel()
    }
}
