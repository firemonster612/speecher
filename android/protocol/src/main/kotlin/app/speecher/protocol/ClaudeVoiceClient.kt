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
    private val pending = mutableListOf<ByteArray>()
    private var socket: WebSocket? = null
    private var connected = false
    private var stopped = false
    private var cancelled = false
    private var completed = false
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
    }

    override fun onOpen(webSocket: WebSocket, response: Response) {
        if (cancelled) return
        socket = webSocket
        connected = true
        webSocket.send("{\"type\":\"KeepAlive\"}")
        keepAlive.scheduleAtFixedRate(
            { if (!cancelled && !completed) webSocket.send("{\"type\":\"KeepAlive\"}") },
            8,
            8,
            TimeUnit.SECONDS,
        )
        pending.forEach { webSocket.send(it.toByteString()) }
        pending.clear()
        if (stopped) closeStream() else events(SpeechEvent.Connected)
    }

    override fun onMessage(webSocket: WebSocket, text: String) {
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
            is ClaudeVoiceEvent.ServerError ->
                events(
                    SpeechEvent.Failed(
                        event.summary.contains("401") || event.summary.contains("403")
                    )
                )
            is ClaudeVoiceEvent.TranscriptError -> events(SpeechEvent.Failed(false))
            ClaudeVoiceEvent.Unknown -> Unit
        }
    }

    override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
        keepAlive.shutdownNow()
        if (!cancelled && !completed)
            events(SpeechEvent.Failed(response?.code == 401 || response?.code == 403))
    }

    override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
        keepAlive.shutdownNow()
        if (!cancelled && !completed) events(SpeechEvent.Failed(false))
    }

    override fun sendAudio(pcm: ByteArray) {
        if (stopped || cancelled) return
        if (connected) socket?.send(pcm.toByteString()) else pending.add(pcm)
    }

    override fun stop() {
        if (stopped || cancelled) return
        stopped = true
        if (connected) closeStream()
    }

    private fun closeStream() {
        socket?.send("{\"type\":\"CloseStream\"}")
    }

    override fun cancel() {
        cancelled = true
        keepAlive.shutdownNow()
        pending.clear()
        socket?.cancel()
    }
}
