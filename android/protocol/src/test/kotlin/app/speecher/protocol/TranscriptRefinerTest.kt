package app.speecher.protocol

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import okhttp3.OkHttpClient
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class TranscriptRefinerTest {
    private val tokens = OAuthTokens("access", "refresh", "", 0, "")

    @Test
    fun `Claude streams refinement with CLI identity and desktop model`() {
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
                TranscriptRefiner(OkHttpClient())
                    .refine(
                        OAuthProvider.Claude,
                        tokens,
                        "helo",
                        listOf("Speecher"),
                        server.url("/v1").toString().trimEnd('/'),
                    )
            assertEquals("Hello", result)
            val request = server.takeRequest()
            assertEquals("/v1/messages", request.target)
            assertEquals("claude-code-20250219,oauth-2025-04-20", request.headers["anthropic-beta"])
            val body = Json.parseToJsonElement(request.body!!.utf8()).jsonObject
            assertEquals("claude-sonnet-5", body["model"]?.jsonPrimitive?.content)
            assertTrue(
                body["system"]
                    .toString()
                    .contains("You are Claude Code, Anthropic's official CLI for Claude.")
            )
            assertTrue(body["messages"].toString().contains("Speecher"))
        }
    }

    @Test
    fun `ChatGPT streams refinement with desktop model and no reasoning`() {
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
                TranscriptRefiner(OkHttpClient())
                    .refine(
                        OAuthProvider.ChatGpt,
                        tokens,
                        "helo",
                        emptyList(),
                        server.url("/codex").toString().trimEnd('/'),
                    )
            assertEquals("Hello", result)
            val request = server.takeRequest()
            assertEquals("/codex/responses", request.target)
            val body = Json.parseToJsonElement(request.body!!.utf8()).jsonObject
            assertEquals("gpt-5.6-luna", body["model"]?.jsonPrimitive?.content)
            assertTrue(body["reasoning"].toString().contains("none"))
            assertEquals("false", body["store"]?.jsonPrimitive?.content)
        }
    }
}
