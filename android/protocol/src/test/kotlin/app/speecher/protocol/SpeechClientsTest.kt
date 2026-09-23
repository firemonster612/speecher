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
    @Test
    fun `Claude authentication error is classified`() {
        MockWebServer().use { server ->
            server.enqueue(
                MockResponse.Builder()
                    .webSocketUpgrade(
                        object : WebSocketListener() {
                            override fun onOpen(webSocket: WebSocket, response: Response) {
                                webSocket.send("""{"type":"error","error":{"code":"401"}}""")
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
                                if (text == "{\"type\":\"CloseStream\"}")
                                    webSocket.send(
                                        """{"type":"TranscriptEndpoint","data":"hello"}"""
                                    )
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
            assertEquals(SpeechEvent.Connected, events.poll(3, TimeUnit.SECONDS))
            client.sendAudio(byteArrayOf(1, 2))
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
