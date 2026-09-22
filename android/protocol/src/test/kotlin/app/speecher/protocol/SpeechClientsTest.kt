package app.speecher.protocol

import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import okhttp3.OkHttpClient
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import okio.ByteString
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertTrue
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
                    OkHttpClient(),
                    "secret",
                    emptyList(),
                    { if (it is SpeechEvent.Failed) failures.add(it) },
                    server.url("/voice").toString().replaceFirst("http", "ws"),
                )
            assertEquals(SpeechEvent.Failed(true), failures.poll(3, TimeUnit.SECONDS))
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
                    OkHttpClient(),
                    "secret",
                    { if (it is SpeechEvent.Failed) failures.add(it) },
                    server.url("/dictation").toString().replaceFirst("http", "ws"),
                )
            assertEquals(SpeechEvent.Failed(true), failures.poll(3, TimeUnit.SECONDS))
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
                                if (text.contains("CloseStream"))
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
                    OkHttpClient(),
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
            assertEquals("linear16", request.url.queryParameter("encoding"))
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
                                if (text.contains("session.start"))
                                    webSocket.send("""{"type":"session.started"}""")
                                if (text.contains("session.close")) {
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
                    OkHttpClient(),
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
            val start = frames.poll(3, TimeUnit.SECONDS)
            assertTrue(start.contains("\"sample_rate_hz\":16000"))
            assertTrue(start.contains("\"type\":\"session.start\""))
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
        }
    }
}
