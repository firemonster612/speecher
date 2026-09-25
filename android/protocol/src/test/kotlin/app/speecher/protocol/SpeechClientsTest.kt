package app.speecher.protocol

import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import okio.ByteString
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Test

class SpeechClientsTest {
    private class FakeTransport : WebSocketTransport {
        lateinit var server: WebSocketTransport.Listener

        override fun open(
            url: String,
            headers: Map<String, String>,
            subprotocol: String?,
            listener: WebSocketTransport.Listener,
        ) {
            server = listener
        }

        override fun sendText(text: String) = true

        override fun sendBinary(bytes: ByteArray) = true

        override fun close(code: Int, reason: String?) {}

        override fun cancel() {}
    }

    /** What a live Codex stream reports when the server closes it with [code]. */
    private fun codexClosed(code: Int): SpeechEvent {
        val transport = FakeTransport()
        val events = mutableListOf<SpeechEvent>()
        CodexDictationClient(transport, "token", events::add).also {
            transport.server.onText("""{"type":"session.started"}""")
            transport.server.onClosed(code, "")
            it.cancel()
        }
        return events.last()
    }

    @Test
    fun `an unrequested close ends the session when normal and is a retryable drop otherwise`() {
        assertEquals(
            listOf(
                SpeechEvent.Completed,
                SpeechEvent.Completed,
                SpeechEvent.Failed(false, "closed 1001", retryable = true),
                SpeechEvent.Failed(false, "closed 1008", retryable = true),
                SpeechEvent.Failed(false, "closed 4001", retryable = true),
            ),
            listOf(1000, 1005, 1001, 1008, 4001).map(::codexClosed),
        )
    }

    @Test
    fun `a Codex error that says it is not retryable is not retried`() {
        val transport = FakeTransport()
        val events = mutableListOf<SpeechEvent>()
        val client = CodexDictationClient(transport, "token", events::add)
        transport.server.onText("""{"type":"session.started"}""")
        transport.server.onText(
            """{"type":"session.error","fatal":true,"error":{"code":"quota","message":"exceeded","retryable":false}}"""
        )
        assertEquals(SpeechEvent.Failed(false, "quota exceeded", retryable = false), events.last())
        client.cancel()
    }

    @Test
    fun `Claude authentication error is classified`() {
        MockWebServer().use { server ->
            server.enqueue(
                MockResponse.Builder()
                    .webSocketUpgrade(
                        object : WebSocketListener() {
                            override fun onOpen(webSocket: WebSocket, response: Response) {
                                webSocket.send("""{"type":"error","error":{"code":"401"}}""")
                                webSocket.close(1000, null)
                            }
                        }
                    )
                    .build()
            )
            server.start()
            val failures = LinkedBlockingQueue<SpeechEvent.Failed>()
            val client =
                ClaudeVoiceClient(
                    webSocketTransport(server.url("/").toString()),
                    "secret",
                    emptyList(),
                    { if (it is SpeechEvent.Failed) failures.add(it) },
                    server.url("/voice").toString().replaceFirst("http", "ws"),
                )
            assertEquals(
                SpeechEvent.Failed(true, "type=error code=401"),
                failures.poll(3, TimeUnit.SECONDS),
            )
            client.cancel()
        }
    }

    @Test
    fun `Codex fatal authentication error is classified`() {
        MockWebServer().use { server ->
            server.enqueue(
                MockResponse.Builder()
                    .addHeader("Sec-WebSocket-Protocol", "chatgpt-dictation")
                    .webSocketUpgrade(
                        object : WebSocketListener() {
                            override fun onOpen(webSocket: WebSocket, response: Response) {
                                webSocket.send(
                                    """{"type":"session.error","fatal":true,"error":{"code":"403","message":"forbidden"}}"""
                                )
                                webSocket.close(1000, null)
                            }
                        }
                    )
                    .build()
            )
            server.start()
            val failures = LinkedBlockingQueue<SpeechEvent.Failed>()
            val client =
                CodexDictationClient(
                    webSocketTransport(server.url("/").toString()),
                    "secret",
                    { if (it is SpeechEvent.Failed) failures.add(it) },
                    server.url("/dictation").toString().replaceFirst("http", "ws"),
                )
            assertEquals(
                SpeechEvent.Failed(true, "403 forbidden"),
                failures.poll(3, TimeUnit.SECONDS),
            )
            client.cancel()
        }
    }

    @Test
    fun `Claude sends query headers binary audio and CloseStream`() {
        val frames = LinkedBlockingQueue<String>()
        MockWebServer().use { server ->
            server.enqueue(
                MockResponse.Builder()
                    .webSocketUpgrade(
                        object : WebSocketListener() {
                            override fun onMessage(webSocket: WebSocket, bytes: ByteString) {
                                frames.add("binary:${bytes.hex()}")
                            }

                            override fun onMessage(webSocket: WebSocket, text: String) {
                                frames.add(text)
                                if (text == "{\"type\":\"CloseStream\"}") {
                                    webSocket.send(
                                        """{"type":"TranscriptEndpoint","data":"hello"}"""
                                    )
                                    webSocket.close(1000, null)
                                }
                            }
                        }
                    )
                    .build()
            )
            server.start()
            val events = LinkedBlockingQueue<SpeechEvent>()
            val client =
                ClaudeVoiceClient(
                    webSocketTransport(server.url("/").toString()),
                    "secret",
                    listOf("Speecher"),
                    events::add,
                    server.url("/voice").toString().replaceFirst("http", "ws"),
                )
            // Sent while the socket is still opening: held and flushed after the handshake.
            client.sendAudio(byteArrayOf(1, 2))
            assertEquals(SpeechEvent.Connected, events.poll(3, TimeUnit.SECONDS))
            client.stop()
            val request = server.takeRequest()
            assertEquals("Bearer secret", request.headers["Authorization"])
            assertEquals("Speecher", request.headers["x-config-keyterms"])
            assertEquals(
                "encoding=linear16&sample_rate=16000&channels=1&endpointing_ms=300&utterance_end_ms=1000&language=en&use_conversation_engine=true&forward_interims=typed&stt_provider=deepgram-nova3",
                request.url.encodedQuery,
            )
            assertEquals("{\"type\":\"KeepAlive\"}", frames.poll(3, TimeUnit.SECONDS))
            assertEquals("binary:0102", frames.poll(3, TimeUnit.SECONDS))
            assertEquals("{\"type\":\"CloseStream\"}", frames.poll(3, TimeUnit.SECONDS))
            assertEquals(SpeechEvent.Final("hello"), events.poll(3, TimeUnit.SECONDS))
            assertEquals(SpeechEvent.Completed, events.poll(3, TimeUnit.SECONDS))
            client.cancel()
        }
    }

    @Test
    fun `Codex starts session buffers audio and closes after flush`() {
        val frames = LinkedBlockingQueue<String>()
        MockWebServer().use { server ->
            server.enqueue(
                MockResponse.Builder()
                    .addHeader("Sec-WebSocket-Protocol", "chatgpt-dictation")
                    .webSocketUpgrade(
                        object : WebSocketListener() {
                            override fun onMessage(webSocket: WebSocket, text: String) {
                                frames.add(text)
                                if (text.startsWith("{\"type\":\"session.start\""))
                                    webSocket.send("""{"type":"session.started"}""")
                                if (text == "{\"type\":\"session.close\"}") {
                                    webSocket.send(
                                        """{"type":"transcript.final","utterance_id":"1","text":"hello"}"""
                                    )
                                    webSocket.send(
                                        """{"type":"session.updated","session":{"status":"closed"}}"""
                                    )
                                    webSocket.close(1000, null)
                                }
                            }
                        }
                    )
                    .build()
            )
            server.start()
            val events = LinkedBlockingQueue<SpeechEvent>()
            val client =
                CodexDictationClient(
                    webSocketTransport(server.url("/").toString()),
                    "secret",
                    events::add,
                    server.url("/dictation").toString().replaceFirst("http", "ws"),
                )
            client.sendAudio(byteArrayOf(1, 2))
            assertEquals(SpeechEvent.Connected, events.poll(3, TimeUnit.SECONDS))
            client.stop()
            val request = server.takeRequest()
            assertEquals(
                "chatgpt-dictation, openai-bearer.secret",
                request.headers["Sec-WebSocket-Protocol"],
            )
            assertEquals(
                "{\"type\":\"session.start\",\"config\":{\"input_audio_format\":\"pcm16\",\"sample_rate_hz\":16000,\"num_channels\":1,\"max_buffer_size_bytes\":4194304,\"max_utterance_duration_ms\":30000,\"session_ttl_ms\":300000,\"provider_mode\":\"streaming_sse\",\"transcript_delivery_mode\":\"segment\",\"vad\":{\"type\":\"server_vad\",\"threshold\":0.5,\"prefix_padding_ms\":300,\"silence_duration_ms\":500}}}",
                frames.poll(3, TimeUnit.SECONDS),
            )
            assertEquals(
                "{\"type\":\"audio.append\",\"audio\":\"AQI=\"}",
                frames.poll(3, TimeUnit.SECONDS),
            )
            assertEquals(
                "{\"type\":\"audio.flush\",\"reason\":\"client\"}",
                frames.poll(3, TimeUnit.SECONDS),
            )
            assertEquals("{\"type\":\"session.close\"}", frames.poll(3, TimeUnit.SECONDS))
            assertEquals(SpeechEvent.Final("hello"), events.poll(3, TimeUnit.SECONDS))
            assertEquals(SpeechEvent.Completed, events.poll(3, TimeUnit.SECONDS))
            client.cancel()
        }
    }
}
