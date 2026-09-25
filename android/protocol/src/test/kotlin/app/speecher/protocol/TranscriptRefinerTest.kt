package app.speecher.protocol

import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import okhttp3.OkHttpClient
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertThrows
import org.junit.jupiter.api.Test

class TranscriptRefinerTest {
    private val tokens = OAuthTokens("access", "refresh", "", 0, "")

    @Test
    fun `Claude streams refinement with CLI identity and the chosen model and effort`() {
        MockWebServer().use { server ->
            server.enqueue(
                MockResponse.Builder()
                    .body(
                        "event: content_block_delta\ndata: {\"delta\":{\"type\":\"text_delta\",\"text\":\"Hello\"}}\n\nevent: message_stop\ndata: {}\n\n"
                    )
                    .build()
            )
            server.start()
            val result =
                refineTranscript(
                    OkHttpClient(),
                    OAuthProvider.Claude,
                    tokens,
                    "helo",
                    listOf("Speecher"),
                    "claude-opus-5",
                    "medium",
                    RefinementContext(),
                    server.url("/v1").toString().trimEnd('/'),
                )
            assertEquals("Hello", result)
            val request = server.takeRequest()
            assertEquals("/v1/messages", request.target)
            assertEquals("claude-code-20250219,oauth-2025-04-20", request.headers["anthropic-beta"])
            val body = Json.parseToJsonElement(request.body!!.utf8()).jsonObject
            assertEquals("claude-opus-5", body["model"]?.jsonPrimitive?.content)
            assertEquals("{\"effort\":\"medium\"}", body["output_config"].toString())
            assertEquals(
                "You are Claude Code, Anthropic's official CLI for Claude.",
                body["system"]!!.jsonArray[0].jsonObject["text"]!!.jsonPrimitive.content,
            )
            assertEquals(
                "Dictation refinement input. Refine raw_transcript using the system instructions and return only the final refined transcript.\n{\"mode\":\"refine_dictation\",\"raw_transcript\":\"helo\"}\n\nPreferred vocabulary:\nSpeecher\n\nBinding aliases:\n",
                body["messages"]!!.jsonArray[0].jsonObject["content"]!!.jsonPrimitive.content,
            )
        }
    }

    @Test
    fun `ChatGPT streams refinement with the chosen model and effort`() {
        MockWebServer().use { server ->
            server.enqueue(
                MockResponse.Builder()
                    .body(
                        "event: response.output_text.delta\ndata: {\"delta\":\"Hello\"}\n\nevent: response.completed\ndata: {}\n\n"
                    )
                    .build()
            )
            server.start()
            val result =
                refineTranscript(
                    OkHttpClient(),
                    OAuthProvider.ChatGpt,
                    tokens,
                    "helo",
                    emptyList(),
                    "gpt-6-luna",
                    "none",
                    RefinementContext(),
                    server.url("/codex").toString().trimEnd('/'),
                )
            assertEquals("Hello", result)
            val request = server.takeRequest()
            assertEquals("/codex/responses", request.target)
            val body = Json.parseToJsonElement(request.body!!.utf8()).jsonObject
            assertEquals("gpt-6-luna", body["model"]?.jsonPrimitive?.content)
            assertEquals("{\"effort\":\"none\"}", body["reasoning"].toString())
            assertEquals("false", body["store"]?.jsonPrimitive?.content)
        }
    }

    @Test
    fun `ChatGPT fast mode asks for the fast tier and drops to standard speed once it is rejected`() {
        MockWebServer().use { server ->
            val ok =
                "event: response.output_text.delta\ndata: {\"delta\":\"Hello\"}\n\nevent: response.completed\ndata: {}\n\n"
            server.enqueue(MockResponse.Builder().code(400).build())
            server.enqueue(MockResponse.Builder().body(ok).build())
            server.enqueue(MockResponse.Builder().body(ok).build())
            server.start()
            val fast = AtomicBoolean(true)
            repeat(2) {
                val result =
                    refineTranscript(
                        OkHttpClient(),
                        OAuthProvider.ChatGpt,
                        tokens,
                        "helo",
                        emptyList(),
                        "gpt-6-luna",
                        "none",
                        RefinementContext(),
                        server.url("/codex").toString(),
                        fast,
                    )
                assertEquals("Hello", result)
            }
            fun tier() =
                Json.parseToJsonElement(server.takeRequest().body!!.utf8())
                    .jsonObject["service_tier"]
                    ?.jsonPrimitive
                    ?.content
            assertEquals(listOf("priority", null, null), List(3) { tier() })
            assertEquals(false, fast.get())
        }
    }

    @Test
    fun `Claude fast mode is sent only for Opus models`() {
        MockWebServer().use { server ->
            val ok =
                "event: content_block_delta\ndata: {\"delta\":{\"type\":\"text_delta\",\"text\":\"Hello\"}}\n\nevent: message_stop\ndata: {}\n\n"
            server.enqueue(MockResponse.Builder().body(ok).build())
            server.enqueue(MockResponse.Builder().body(ok).build())
            server.start()
            for (model in listOf("claude-opus-5", "claude-sonnet-5")) {
                refineTranscript(
                    OkHttpClient(),
                    OAuthProvider.Claude,
                    tokens,
                    "helo",
                    emptyList(),
                    model,
                    "low",
                    RefinementContext(),
                    server.url("/v1").toString(),
                    AtomicBoolean(true),
                )
            }
            val requests = List(2) { server.takeRequest() }
            assertEquals(
                listOf(
                    "claude-code-20250219,oauth-2025-04-20,fast-mode-2026-02-01",
                    "claude-code-20250219,oauth-2025-04-20",
                ),
                requests.map { it.headers["anthropic-beta"] },
            )
            assertEquals(
                listOf("fast", null),
                requests.map {
                    Json.parseToJsonElement(it.body!!.utf8())
                        .jsonObject["speed"]
                        ?.jsonPrimitive
                        ?.content
                },
            )
        }
    }

    @Test
    fun `Claude attaches a captured screenshot as a base64 image block after the text`() {
        MockWebServer().use { server ->
            server.enqueue(
                MockResponse.Builder()
                    .body(
                        "event: content_block_delta\ndata: {\"delta\":{\"type\":\"text_delta\",\"text\":\"Hello\"}}\n\nevent: message_stop\ndata: {}\n\n"
                    )
                    .build()
            )
            server.start()
            refineTranscript(
                OkHttpClient(),
                OAuthProvider.Claude,
                tokens,
                "helo",
                emptyList(),
                "claude-sonnet-5",
                "low",
                RefinementContext(screenshotJpeg = "AAAA"),
                server.url("/v1").toString().trimEnd('/'),
            )
            val body = Json.parseToJsonElement(server.takeRequest().body!!.utf8()).jsonObject
            val content = body["messages"]!!.jsonArray[0].jsonObject["content"]!!.jsonArray
            assertEquals("text", content[0].jsonObject["type"]!!.jsonPrimitive.content)
            assertEquals(
                "{\"type\":\"image\",\"source\":{\"type\":\"base64\",\"media_type\":\"image/jpeg\",\"data\":\"AAAA\"}}",
                content[1].toString(),
            )
        }
    }

    @Test
    fun `ChatGPT retries without the screenshot when the model rejects it`() {
        MockWebServer().use { server ->
            server.enqueue(MockResponse.Builder().code(400).build())
            server.enqueue(
                MockResponse.Builder()
                    .body(
                        "event: response.output_text.delta\ndata: {\"delta\":\"Hello\"}\n\nevent: response.completed\ndata: {}\n\n"
                    )
                    .build()
            )
            server.start()
            val result =
                refineTranscript(
                    OkHttpClient(),
                    OAuthProvider.ChatGpt,
                    tokens,
                    "helo",
                    emptyList(),
                    "gpt-6-luna",
                    "none",
                    RefinementContext(screenshotJpeg = "AAAA"),
                    server.url("/codex").toString().trimEnd('/'),
                )
            assertEquals("Hello", result)
            fun content() =
                Json.parseToJsonElement(server.takeRequest().body!!.utf8())
                    .jsonObject["input"]!!
                    .jsonArray[0]
                    .jsonObject["content"]!!
            assertEquals(
                "{\"type\":\"input_image\",\"image_url\":\"data:image/jpeg;base64,AAAA\",\"detail\":\"low\"}",
                content().jsonArray[1].toString(),
            )
            assertEquals(true, content() is JsonPrimitive)
        }
    }

    @Test
    fun `a fast-mode refusal with a screenshot retries at standard speed with the image first`() {
        MockWebServer().use { server ->
            server.enqueue(MockResponse.Builder().code(400).build())
            server.enqueue(
                MockResponse.Builder()
                    .body(
                        "event: response.output_text.delta\ndata: {\"delta\":\"Hello\"}\n\nevent: response.completed\ndata: {}\n\n"
                    )
                    .build()
            )
            server.start()
            refineTranscript(
                OkHttpClient(),
                OAuthProvider.ChatGpt,
                tokens,
                "helo",
                emptyList(),
                "gpt-6-luna",
                "none",
                RefinementContext(screenshotJpeg = "AAAA"),
                server.url("/codex").toString(),
                AtomicBoolean(true),
            )
            val requests =
                List(2) { Json.parseToJsonElement(server.takeRequest().body!!.utf8()).jsonObject }
            assertEquals(
                listOf("priority" to true, null to true),
                requests.map { body ->
                    body["service_tier"]?.jsonPrimitive?.content to
                        (body["input"]!!.jsonArray[0].jsonObject["content"] !is JsonPrimitive)
                },
            )
        }
    }

    @Test
    fun `fast mode is not retried once refined text has streamed`() {
        MockWebServer().use { server ->
            server.enqueue(
                MockResponse.Builder()
                    .body("event: response.output_text.delta\ndata: {\"delta\":\"Hel\"}\n\n")
                    .build()
            )
            server.start()
            val fast = AtomicBoolean(true)
            val shown = mutableListOf<String>()
            assertThrows(IllegalStateException::class.java) {
                refineTranscript(
                    OkHttpClient(),
                    OAuthProvider.ChatGpt,
                    tokens,
                    "helo",
                    emptyList(),
                    "gpt-6-luna",
                    "none",
                    RefinementContext(),
                    server.url("/codex").toString(),
                    fast,
                    shown::add,
                )
            }
            assertEquals(
                Triple(listOf("Hel"), 1, true),
                Triple(shown, server.requestCount, fast.get()),
            )
        }
    }

    @Test
    fun `a rate limit with a screenshot attached fails without a second request`() {
        MockWebServer().use { server ->
            server.enqueue(MockResponse.Builder().code(429).build())
            server.start()
            val failure =
                assertThrows(RefinementHttpError::class.java) {
                    refineTranscript(
                        OkHttpClient(),
                        OAuthProvider.ChatGpt,
                        tokens,
                        "helo",
                        emptyList(),
                        "gpt-6-luna",
                        "none",
                        RefinementContext(screenshotJpeg = "AAAA"),
                        server.url("/codex").toString().trimEnd('/'),
                    )
                }
            assertEquals(429 to 1, failure.status to server.requestCount)
        }
    }

    @Test
    fun `unfinished stream does not return partial refinement`() {
        MockWebServer().use { server ->
            server.enqueue(
                MockResponse.Builder()
                    .body("event: response.output_text.delta\ndata: {\"delta\":\"partial\"}\n\n")
                    .build()
            )
            server.start()
            assertThrows(IllegalStateException::class.java) {
                refineTranscript(
                    OkHttpClient(),
                    OAuthProvider.ChatGpt,
                    tokens,
                    "raw",
                    emptyList(),
                    "gpt-6-luna",
                    "none",
                    RefinementContext(),
                    server.url("/codex").toString().trimEnd('/'),
                )
            }
        }
    }
}
