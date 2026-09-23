package app.speecher.protocol

import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import okhttp3.OkHttpClient
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertThrows
import org.junit.jupiter.api.Test

class OAuthTest {
    @Test
    fun `Claude authorization uses fixed redirect and PKCE`() {
        val claude = oauthAttempt(OAuthProvider.Claude)
        assertEquals(
            "http://localhost:54545/callback",
            claude.authorizeUrl.queryParameter("redirect_uri"),
        )
        assertEquals("S256", claude.authorizeUrl.queryParameter("code_challenge_method"))
        assertEquals("true", claude.authorizeUrl.queryParameter("code"))
    }

    @Test
    fun `ChatGPT authorization uses fixed redirect and originator`() {
        val chatGpt = oauthAttempt(OAuthProvider.ChatGpt)
        assertEquals(
            "http://localhost:1455/auth/callback",
            chatGpt.authorizeUrl.queryParameter("redirect_uri"),
        )
        assertEquals("codex_cli_rs", chatGpt.authorizeUrl.queryParameter("originator"))
    }

    @Test
    fun `Claude callback and pasted code require matching state`() {
        val claude = oauthAttempt(OAuthProvider.Claude)
        assertEquals(
            "code",
            oauthCallback(
                OAuthProvider.Claude,
                claude,
                "/callback",
                "code=code&state=${claude.state}",
            ),
        )
        assertEquals("code", pastedCode(claude, "code#${claude.state}"))
        assertThrows(IllegalArgumentException::class.java) { pastedCode(claude, "code#wrong") }
    }

    @Test
    fun `pasted code accepts a full redirect URL, a raw query, or a bare code`() {
        val attempt = oauthAttempt(OAuthProvider.ChatGpt)
        val state = attempt.state
        assertEquals(
            "abc",
            pastedCode(attempt, "http://localhost:1455/auth/callback?code=abc&state=$state"),
        )
        assertEquals("abc", pastedCode(attempt, "code=abc&state=$state"))
        assertEquals(
            "a b",
            pastedCode(attempt, "http://localhost:1455/auth/callback?code=a%20b&state=$state"),
        )
        assertEquals("bare", pastedCode(attempt, "  bare  "))
        assertThrows(IllegalArgumentException::class.java) {
            pastedCode(attempt, "http://localhost:1455/auth/callback?code=abc&state=wrong")
        }
    }

    @Test
    fun `an attempt restored from disk still validates a pasted callback`() {
        val attempt = oauthAttempt(OAuthProvider.ChatGpt)
        val restored = restoredAttempt(attempt.verifier, attempt.state)
        assertEquals(attempt.verifier, restored.verifier)
        assertEquals(
            "abc",
            pastedCode(
                restored,
                "http://localhost:1455/auth/callback?code=abc&state=${attempt.state}",
            ),
        )
    }

    @Test
    fun `Claude exchanges JSON and refreshes JSON`() {
        MockWebServer().use { server ->
            server.enqueue(
                MockResponse.Builder()
                    .body(
                        """{"access_token":"a","refresh_token":"r","expires_in":60,"scope":"returned-scope"}"""
                    )
                    .build()
            )
            server.enqueue(
                MockResponse.Builder().body("""{"access_token":"b","expires_in":90}""").build()
            )
            server.start()

            val attempt = oauthAttempt(OAuthProvider.Claude)
            val first =
                exchangeTokens(
                    OkHttpClient(),
                    OAuthProvider.Claude,
                    attempt,
                    "code",
                    { 1000 },
                    server.url("/token").toString(),
                )
            val request = server.takeRequest()
            assertEquals("application/json", request.headers["Content-Type"])
            assertEquals(
                "{\"grant_type\":\"authorization_code\",\"code\":\"code\",\"redirect_uri\":\"http://localhost:54545/callback\",\"client_id\":\"9d1c250a-e61b-44d9-88ed-5944d1962f5e\",\"code_verifier\":\"${attempt.verifier}\",\"state\":\"${attempt.state}\"}",
                request.body!!.utf8(),
            )
            // The Claude Code Axios header set that clears Cloudflare on Anthropic domains.
            assertEquals("axios/1.15.2", request.headers["User-Agent"])
            assertEquals("application/json, text/plain, */*", request.headers["Accept"])
            assertEquals("gzip, compress, deflate, br", request.headers["Accept-Encoding"])
            assertEquals("close", request.headers["Connection"])
            val refreshed =
                refreshTokens(
                    OkHttpClient(),
                    OAuthProvider.Claude,
                    first,
                    { 1000 },
                    server.url("/token").toString(),
                )
            assertEquals("r", refreshed.refreshToken)
            assertEquals(91000, refreshed.expiresAtMillis)
            val refreshRequest = server.takeRequest()
            assertEquals("application/json", refreshRequest.headers["Content-Type"])
            assertEquals(
                "{\"grant_type\":\"refresh_token\",\"refresh_token\":\"r\",\"client_id\":\"9d1c250a-e61b-44d9-88ed-5944d1962f5e\",\"scope\":\"user:profile user:inference user:sessions:claude_code user:mcp_servers user:file_upload\"}",
                refreshRequest.body!!.utf8(),
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

            val attempt = oauthAttempt(OAuthProvider.ChatGpt)
            val first =
                exchangeTokens(
                    OkHttpClient(),
                    OAuthProvider.ChatGpt,
                    attempt,
                    "code",
                    { 0 },
                    server.url("/token").toString(),
                )
            val request = server.takeRequest()
            assertEquals("application/x-www-form-urlencoded", request.headers["Content-Type"])
            assertEquals(
                "grant_type=authorization_code&code=code&redirect_uri=http%3A%2F%2Flocalhost%3A1455%2Fauth%2Fcallback&client_id=app_EMoamEEZ73f0CkXaXp7hrann&code_verifier=${attempt.verifier}",
                request.body!!.utf8(),
            )
            val second =
                refreshTokens(
                    OkHttpClient(),
                    OAuthProvider.ChatGpt,
                    first,
                    { 0 },
                    server.url("/token").toString(),
                )
            assertEquals("id", second.idToken)
            assertEquals("r2", second.refreshToken)
            val refreshRequest = server.takeRequest()
            assertEquals("application/json", refreshRequest.headers["Content-Type"])
            assertEquals(
                "{\"grant_type\":\"refresh_token\",\"refresh_token\":\"r\",\"client_id\":\"app_EMoamEEZ73f0CkXaXp7hrann\",\"scope\":\"openid profile email\"}",
                refreshRequest.body!!.utf8(),
            )
        }
    }

    @Test
    fun `refresh exposes revoked token status`() {
        MockWebServer().use { server ->
            server.enqueue(MockResponse.Builder().code(401).build())
            server.start()
            val error =
                assertThrows(OAuthHttpException::class.java) {
                    refreshTokens(
                        OkHttpClient(),
                        OAuthProvider.Claude,
                        OAuthTokens("a", "r", "", 0, ""),
                        tokenUrl = server.url("/token").toString(),
                    )
                }
            assertEquals(401, error.status)
        }
    }
}
