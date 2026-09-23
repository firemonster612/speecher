package app.speecher.protocol

import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import okhttp3.HttpUrl.Companion.toHttpUrl
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import okio.ByteString.Companion.toByteString

class ClaudeVoiceClient(
    http: OkHttpClient,
    token: String,
    vocabulary: List<String>,
    private val events: (SpeechEvent) -> Unit,
    endpoint: String = "wss://claude.ai/api/ws/speech_to_text/voice_stream",
) : WebSocketListener(), SpeechClient {
    private val lock = Any()
    private val pending = mutableListOf<ByteArray>()
    private var pendingBytes = 0
    private var socket: WebSocket? = null
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
        val request =
            Request.Builder()
                .url(url)
                .header("Authorization", "Bearer $token")
                .header("User-Agent", "Claude-Code")
                .header("x-app", "cli")
                .header("anthropic-client-platform", "linux")
                .apply {
                    claudeVoiceKeytermsHeader(vocabulary).takeIf(String::isNotEmpty)?.let {
                        header("x-config-keyterms", it)
                    }
                }
                .build()
        socket = http.newWebSocket(request, this)
        keepAlive.schedule({ if (!connected) fail(false) }, 10, TimeUnit.SECONDS)
    }

    override fun onOpen(webSocket: WebSocket, response: Response) {
        if (cancelled || failed) return
        val buffered =
            synchronized(lock) {
                connected = true
                pending.toList().also {
                    pending.clear()
                    pendingBytes = 0
                }
            }
        webSocket.send("{\"type\":\"KeepAlive\"}")
        keepAlive.scheduleAtFixedRate(
            { if (!cancelled && !completed) webSocket.send("{\"type\":\"KeepAlive\"}") },
            8,
            8,
            TimeUnit.SECONDS,
        )
        buffered.forEach { webSocket.send(it.toByteString()) }
        if (stopped) closeStream() else events(SpeechEvent.Connected)
    }

    override fun onMessage(webSocket: WebSocket, text: String) {
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
                    webSocket.close(1000, null)
                }
            }
            is ClaudeVoiceEvent.ServerError -> fail(isAuthenticationError(event.summary))
            is ClaudeVoiceEvent.TranscriptError -> fail(false)
            ClaudeVoiceEvent.Unknown -> Unit
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
            fail(false)
            return
        }
        if (direct) socket?.send(pcm.toByteString())
    }

    override fun stop() {
        if (stopped || cancelled) return
        stopped = true
        if (connected) closeStream()
        else keepAlive.schedule({ if (!connected) fail(false) }, 5, TimeUnit.SECONDS)
    }

    private fun closeStream() {
        socket?.send("{\"type\":\"CloseStream\"}")
        keepAlive.schedule({ if (!completed) fail(false) }, 5, TimeUnit.SECONDS)
    }

    override fun cancel() {
        cancelled = true
        keepAlive.shutdownNow()
        synchronized(lock) {
            pending.clear()
            pendingBytes = 0
        }
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
        keepAlive.shutdownNow()
        events(SpeechEvent.Failed(authentication))
        socket?.cancel()
    }
}
