package app.speecher.protocol

import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import okhttp3.OkHttpClient
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class EndpointSpeechTest {
    private val http = OkHttpClient()

    /** Records audio, stops, and returns every event up to the last. */
    private fun transcribe(
        endpoint: SpeechEndpoint,
        apiKey: String = "",
        prompt: String = "",
        language: String = AUTOMATIC_LANGUAGE,
        pcm: ByteArray = byteArrayOf(1, 2),
    ): List<SpeechEvent> {
        val events = LinkedBlockingQueue<SpeechEvent>()
        val client = EndpointSpeechClient(http, endpoint, apiKey, prompt, language, events::put)
        client.sendAudio(pcm)
        client.stop()
        return buildList {
            do {
                val event = events.poll(5, TimeUnit.SECONDS)!!
                add(event)
            } while (event != SpeechEvent.Completed && event !is SpeechEvent.Failed)
        }
    }

    /** Each form field's name and value, the file's as its filename and type. */
    private fun formFields(body: String, boundary: String): List<Pair<String, String>> =
        body.split("--$boundary").drop(1).dropLast(1).map { part ->
            val (head, value) = part.trim('\r', '\n').split("\r\n\r\n", limit = 2)
            val name = Regex("name=\"([^\"]+)\"").find(head)!!.groupValues[1]
            val file = Regex("filename=\"([^\"]+)\"").find(head)?.groupValues?.get(1)
            name to
                if (file == null) value
                else "$file ${Regex("Content-Type: (\\S+)").find(head)!!.groupValues[1]}"
        }

    @Test
    fun `uploads the recording as WAV with the desktop's form fields and reads the JSON text`() {
        MockWebServer().use { server ->
            server.enqueue(MockResponse.Builder().body("{\"text\":\" Hello there. \"}").build())
            server.start()
            val events =
                transcribe(
                    SpeechEndpoint(server.url("/v1/").toString(), "inference", " whisper-1 "),
                    apiKey = "secret",
                    prompt = "Speecher, Kirigami",
                    language = "de",
                )
            assertEquals(listOf(SpeechEvent.Final("Hello there."), SpeechEvent.Completed), events)
            val request = server.takeRequest()
            assertEquals("/inference", request.target)
            assertEquals("Bearer secret", request.headers["Authorization"])
            val boundary = request.headers["Content-Type"]!!.substringAfter("boundary=")
            assertEquals(
                listOf(
                    "file" to "dictation.wav audio/wav",
                    "model" to "whisper-1",
                    "response_format" to "json",
                    "language" to "de",
                    "prompt" to "Speecher, Kirigami",
                    "stream" to "true",
                ),
                formFields(request.body!!.string(Charsets.ISO_8859_1), boundary),
            )
        }
    }

    @Test
    fun `leaves out an empty key, model and prompt and an Automatic language`() {
        MockWebServer().use { server ->
            server.enqueue(MockResponse.Builder().body("{\"text\":\"\"}").build())
            server.start()
            assertEquals(
                listOf(SpeechEvent.Completed),
                transcribe(SpeechEndpoint(server.url("/").toString())),
            )
            val request = server.takeRequest()
            assertEquals("/v1/audio/transcriptions", request.target)
            assertNull(request.headers["Authorization"])
            val boundary = request.headers["Content-Type"]!!.substringAfter("boundary=")
            assertEquals(
                listOf("file", "response_format", "stream"),
                formFields(request.body!!.string(Charsets.ISO_8859_1), boundary).map { it.first },
            )
        }
    }

    @Test
    fun `shows streamed deltas as they arrive and inserts the done text`() {
        MockWebServer().use { server ->
            server.enqueue(
                MockResponse.Builder()
                    .addHeader("Content-Type", "text/event-stream")
                    .body(
                        "data: {\"type\":\"transcript.text.delta\",\"delta\":\"Hello\"}\n\n" +
                            "data: {\"type\":\"transcript.text.delta\",\"delta\":\" there\"}\n\n" +
                            "data: {\"type\":\"transcript.text.done\",\"text\":\"Hello there.\"}\n\n"
                    )
                    .build()
            )
            server.start()
            assertEquals(
                listOf(
                    SpeechEvent.Partial("Hello"),
                    SpeechEvent.Partial("Hello there"),
                    SpeechEvent.Final("Hello there."),
                    SpeechEvent.Completed,
                ),
                transcribe(SpeechEndpoint(server.url("/").toString())),
            )
        }
    }

    @Test
    fun `spaces untyped segments except beside a script written without spaces`() {
        MockWebServer().use { server ->
            server.enqueue(
                MockResponse.Builder()
                    .addHeader("Content-Type", "text/event-stream")
                    .body(
                        "data: {\"text\":\" One.\"}\n\n" +
                            "data: {\"text\":\" Two.\"}\n\n" +
                            "data: {\"text\":\"日本語。\"}\n\n" +
                            "data: {\"text\":\"Three\"}\n\n"
                    )
                    .build()
            )
            server.start()
            assertEquals(
                SpeechEvent.Final("One. Two.日本語。Three"),
                transcribe(SpeechEndpoint(server.url("/").toString())).dropLast(1).last(),
            )
        }
    }

    @Test
    fun `a streamed error keeps the text that came first and fails as its kind`() {
        MockWebServer().use { server ->
            server.enqueue(
                MockResponse.Builder()
                    .addHeader("Content-Type", "text/event-stream")
                    .body(
                        "data: {\"type\":\"transcript.text.delta\",\"delta\":\"Hello \"}\n\n" +
                            "event: error\ndata: {\"error\":{\"code\":\"rate_limit_exceeded\"}}\n\n"
                    )
                    .build()
            )
            server.start()
            val events = transcribe(SpeechEndpoint(server.url("/").toString()))
            assertEquals(SpeechEvent.Final("Hello"), events[1])
            assertEquals(ProviderFailureKind.RateLimited, (events[2] as SpeechEvent.Failed).kind)
        }
    }

    @Test
    fun `a 404 from the server is Unavailable and a refused key Authentication`() {
        MockWebServer().use { server ->
            server.enqueue(MockResponse.Builder().code(404).build())
            server.enqueue(MockResponse.Builder().code(401).build())
            server.start()
            val endpoint = SpeechEndpoint(server.url("/").toString())
            assertEquals(
                SpeechEvent.Failed(
                    ProviderFailureKind.Unavailable,
                    "Speech endpoint failed with HTTP 404",
                ),
                transcribe(endpoint).single(),
            )
            assertEquals(
                ProviderFailureKind.Authentication,
                (transcribe(endpoint).single() as SpeechEvent.Failed).kind,
            )
        }
    }

    @Test
    fun `nothing recorded completes without a request`() {
        MockWebServer().use { server ->
            server.start()
            assertEquals(
                listOf(SpeechEvent.Completed),
                transcribe(SpeechEndpoint(server.url("/").toString()), pcm = byteArrayOf()),
            )
            assertEquals(0, server.requestCount)
        }
    }

    @Test
    fun `the server base drops trailing slashes and v1 segments`() {
        assertEquals("http://host:8080", endpointServerBase(" http://host:8080/v1/v1/ "))
        assertEquals("http://host:8080/api", endpointServerBase("http://host:8080/api/"))
    }

    @Test
    fun `connection test lists the server's models from either listing shape`() {
        MockWebServer().use { server ->
            server.enqueue(
                MockResponse.Builder()
                    .body(
                        "{\"data\":[{\"id\":\"whisper-1\"},{\"id\":\"\"}],\"models\":[{\"name\":\"base\"}]}"
                    )
                    .build()
            )
            server.start()
            val base = server.url("/").toString()
            assertEquals(
                EndpointCheck(true, "Connected. Models available: 2.", listOf("whisper-1", "base")),
                checkSpeechEndpoint(http, SpeechEndpoint("$base/v1"), "secret"),
            )
            val request = server.takeRequest()
            assertEquals("/v1/models", request.target)
            assertEquals("Bearer secret", request.headers["Authorization"])
        }
    }

    @Test
    fun `connection test explains a refused key, no listing, no URL and no answer`() {
        MockWebServer().use { server ->
            server.enqueue(MockResponse.Builder().code(403).build())
            server.enqueue(MockResponse.Builder().code(404).build())
            server.start()
            val endpoint = SpeechEndpoint(server.url("/").toString())
            assertEquals(
                EndpointCheck(false, "The server refused the API key (HTTP 403)."),
                checkSpeechEndpoint(http, endpoint, ""),
            )
            assertEquals(
                EndpointCheck(true, "Connected. The server does not list its models; type one."),
                checkSpeechEndpoint(http, endpoint, ""),
            )
        }
        assertEquals(
            EndpointCheck(false, "Enter the server URL."),
            checkSpeechEndpoint(http, SpeechEndpoint(" / "), ""),
        )
        val unreachable = checkSpeechEndpoint(http, SpeechEndpoint("notaurl"), "")
        assertFalse(unreachable.ok)
        assertTrue(unreachable.message.startsWith("Could not reach notaurl: "))
    }
}
