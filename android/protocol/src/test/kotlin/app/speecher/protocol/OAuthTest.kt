package app.speecher.protocol

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import okhttp3.OkHttpClient
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertThrows
import org.junit.jupiter.api.Test

class OAuthTest {
    @Test
    fun `authorization uses fixed redirects and PKCE`() {
        val claude = oauthAttempt(OAuthProvider.Claude)
        assertEquals(
            "http://localhost:54545/callback",
            claude.authorizeUrl.queryParameter("redirect_uri"),
        )
        assertEquals("S256", claude.authorizeUrl.queryParameter("code_challenge_method"))
        assertEquals("true", claude.authorizeUrl.queryParameter("code"))
        val chatGpt = oauthAttempt(OAuthProvider.ChatGpt)
        assertEquals(
            "http://localhost:1455/auth/callback",
            chatGpt.authorizeUrl.queryParameter("redirect_uri"),
        )
        assertEquals("codex_cli_rs", chatGpt.authorizeUrl.queryParameter("originator"))
        assertEquals(
            "code",
            oauthCallback(
                OAuthProvider.Claude,
                claude,
                "/callback",
                "code=code&state=${claude.state}",
            ),
        )
        assertEquals("code", pastedClaudeCode(claude, "code#${claude.state}"))
        assertThrows(IllegalArgumentException::class.java) {
            pastedClaudeCode(claude, "code#wrong")
        }
    }

    @Test
    fun `Claude exchanges JSON and refreshes JSON`() {
        MockWebServer().use { server ->
            server.enqueue(
                MockResponse.Builder()
                    .body("""{"access_token":"a","refresh_token":"r","expires_in":60}""")
                    .build()
            )
            server.enqueue(
                MockResponse.Builder().body("""{"access_token":"b","expires_in":90}""").build()
            )
            server.start()
            val client = OAuthTokenClient(OkHttpClient()) { 1000 }
            val first =
                client.exchange(
                    OAuthProvider.Claude,
                    oauthAttempt(OAuthProvider.Claude),
                    "code",
                    server.url("/token").toString(),
                )
            val request = server.takeRequest()
            assertEquals("application/json; charset=utf-8", request.headers["Content-Type"])
            assertEquals(
                "authorization_code",
                Json.parseToJsonElement(request.body!!.utf8())
                    .jsonObject["grant_type"]
                    ?.jsonPrimitive
                    ?.content,
            )
            assertEquals("axios/1.15.2", request.headers["User-Agent"])
            val refreshed =
                client.refresh(OAuthProvider.Claude, first, server.url("/token").toString())
            assertEquals("r", refreshed.refreshToken)
            assertEquals(91000, refreshed.expiresAtMillis)
            assertEquals(
                "refresh_token",
                Json.parseToJsonElement(server.takeRequest().body!!.utf8())
                    .jsonObject["grant_type"]
                    ?.jsonPrimitive
                    ?.content,
            )
        }
    }

    @Test
    fun `ChatGPT exchanges form and refreshes JSON like desktop`() {
        MockWebServer().use { server ->
            server.enqueue(
                MockResponse.Builder()
                    .body("""{"access_token":"a","refresh_token":"r","id_token":"id"}""")
                    .build()
            )
            server.enqueue(
                MockResponse.Builder().body("""{"access_token":"b","refresh_token":"r2"}""").build()
            )
            server.start()
            val client = OAuthTokenClient(OkHttpClient()) { 0 }
            val first =
                client.exchange(
                    OAuthProvider.ChatGpt,
                    oauthAttempt(OAuthProvider.ChatGpt),
                    "code",
                    server.url("/token").toString(),
                )
            val request = server.takeRequest()
            assertEquals("application/x-www-form-urlencoded", request.headers["Content-Type"])
            assertEquals(true, request.body!!.utf8().contains("grant_type=authorization_code"))
            val second =
                client.refresh(OAuthProvider.ChatGpt, first, server.url("/token").toString())
            assertEquals("id", second.idToken)
            assertEquals("r2", second.refreshToken)
            assertEquals(
                "openid profile email",
                Json.parseToJsonElement(server.takeRequest().body!!.utf8())
                    .jsonObject["scope"]
                    ?.jsonPrimitive
                    ?.content,
            )
        }
    }
}
