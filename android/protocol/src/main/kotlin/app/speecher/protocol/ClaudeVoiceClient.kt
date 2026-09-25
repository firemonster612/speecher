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
            { if (!connected) fail(false, "no connect in 10s", retryable = true) },
            10,
            TimeUnit.SECONDS,
        )
    }

    override fun onOpen() {
        if (cancelled || failed) return
        transport.sendText("{\"type\":\"KeepAlive\"}")
        keepAlive.scheduleAtFixedRate(
            { if (!cancelled && !completed) transport.sendText("{\"type\":\"KeepAlive\"}") },
            8,
            8,
            TimeUnit.SECONDS,
        )
        // Flushed under the lock so audio captured meanwhile queues behind it, not ahead.
        synchronized(lock) {
            connected = true
            pending.forEach { transport.sendBinary(it) }
            pending.clear()
            pendingBytes = 0
        }
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
                    complete()
                    transport.close(1000, null)
                }
            }
            is ClaudeVoiceEvent.ServerError -> {
                val authentication = isAuthenticationError(event.summary)
                fail(authentication, event.summary.take(120), retryable = !authentication)
            }
            is ClaudeVoiceEvent.TranscriptError ->
                fail(false, event.summary.take(120), retryable = true)
            ClaudeVoiceEvent.Unknown -> Unit
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
        if (connected && !stopped && endsSession(code)) complete()
        else fail(false, "closed $code ${reason.take(80)}".trim(), retryable = true)
    }

    override fun sendAudio(pcm: ByteArray) {
        if (stopped || cancelled || failed || pcm.isEmpty()) return
        synchronized(lock) {
            if (connected) {
                transport.sendBinary(pcm)
                return
            }
            if (pendingBytes + pcm.size <= 4 * 1024 * 1024) {
                pending.add(pcm)
                pendingBytes += pcm.size
                return
            }
        }
        fail(false, "audio buffer overflow")
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

    private fun complete() {
        val first =
            synchronized(lock) {
                if (cancelled || completed || failed) false
                else {
                    completed = true
                    true
                }
            }
        if (!first) return
        keepAlive.shutdownNow()
        events(SpeechEvent.Completed)
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
        keepAlive.shutdownNow()
        events(SpeechEvent.Failed(authentication, detail, retryable))
        transport.cancel()
    }
}
