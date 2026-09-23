package app.speecher.protocol

import java.security.MessageDigest
import java.security.SecureRandom
import java.util.Base64
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.jsonPrimitive
import okhttp3.FormBody
import okhttp3.HttpUrl
import okhttp3.HttpUrl.Companion.toHttpUrl
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody

enum class OAuthProvider {
    Claude,
    ChatGpt,
}

data class OAuthTokens(
    val accessToken: String,
    val refreshToken: String,
    val idToken: String,
    val expiresAtMillis: Long,
    val scope: String,
)

data class OAuthAttempt(val verifier: String, val state: String, val authorizeUrl: HttpUrl)

class OAuthHttpException(val status: Int) : Exception("OAuth token endpoint returned HTTP $status")

private const val CLAUDE_CLIENT_ID = "9d1c250a-e61b-44d9-88ed-5944d1962f5e"
private const val CHATGPT_CLIENT_ID = "app_EMoamEEZ73f0CkXaXp7hrann"
private const val CLAUDE_SCOPE =
    "user:profile user:inference user:sessions:claude_code user:mcp_servers user:file_upload"
private const val CHATGPT_SCOPE =
    "openid profile email offline_access api.connectors.read api.connectors.invoke"

fun oauthAttempt(provider: OAuthProvider, random: SecureRandom = SecureRandom()): OAuthAttempt {
    fun secret(): String = ByteArray(32).also(random::nextBytes).base64Url()
    val verifier = secret()
    val state = secret()
    val challenge = MessageDigest.getInstance("SHA-256").digest(verifier.toByteArray()).base64Url()
    val claude = provider == OAuthProvider.Claude
    val url =
        (if (claude) "https://claude.ai/oauth/authorize"
            else "https://auth.openai.com/oauth/authorize")
            .toHttpUrl()
            .newBuilder()
            .addQueryParameter("client_id", if (claude) CLAUDE_CLIENT_ID else CHATGPT_CLIENT_ID)
            .addQueryParameter("response_type", "code")
            .addQueryParameter("redirect_uri", oauthRedirect(provider))
            .addQueryParameter("scope", if (claude) CLAUDE_SCOPE else CHATGPT_SCOPE)
            .addQueryParameter("code_challenge", challenge)
            .addQueryParameter("code_challenge_method", "S256")
            .addQueryParameter("state", state)
            .apply {
                if (claude) addQueryParameter("code", "true")
                else {
                    addQueryParameter("id_token_add_organizations", "true")
                    addQueryParameter("codex_cli_simplified_flow", "true")
                    addQueryParameter("originator", "codex_cli_rs")
                }
            }
            .build()
    return OAuthAttempt(verifier, state, url)
}

fun oauthRedirect(provider: OAuthProvider): String =
    if (provider == OAuthProvider.Claude) "http://localhost:54545/callback"
    else "http://localhost:1455/auth/callback"

/**
 * Rebuild a pending attempt from a persisted verifier and state so a pasted callback can finish
 * even after the app was killed during the browser trip. The authorize URL is not restored, since
 * only the code exchange (which uses the verifier and state) remains.
 */
fun restoredAttempt(verifier: String, state: String): OAuthAttempt =
    OAuthAttempt(verifier, state, "https://localhost/".toHttpUrl())

fun oauthCallback(
    provider: OAuthProvider,
    attempt: OAuthAttempt,
    path: String,
    query: String,
): String {
    val callback = "http://localhost$path?$query".toHttpUrl()
    require(path == oauthRedirect(provider).toHttpUrl().encodedPath) {
        "Unexpected OAuth callback path"
    }
    require(callback.queryParameter("state") == attempt.state) { "OAuth state mismatch" }
    return callback.queryParameter("code")?.takeIf(String::isNotEmpty)
        ?: error("OAuth callback has no code")
}

/**
 * The authorization code out of whatever the user pasted after a browser sign-in: the whole
 * redirect URL (`http://localhost:1455/auth/callback?code=...&state=...`), a bare
 * `code=...&state=...` query, Anthropic's `code#state`, or just the code. Any state that travels
 * with it must match this attempt, so a code from someone else's sign-in is rejected.
 */
fun pastedCode(attempt: OAuthAttempt, pasted: String): String {
    val text = pasted.trim().trim('"')
    require(text.isNotEmpty()) { "Paste the code or the sign-in link from your browser." }

    if ("code=" in text) {
        val params =
            text.substringAfter('?', missingDelimiterValue = text).split('&').mapNotNull {
                val eq = it.indexOf('=')
                if (eq <= 0) null else it.take(eq) to urlDecode(it.substring(eq + 1))
            }
        val code = params.firstOrNull { it.first == "code" }?.second?.substringBefore('#').orEmpty()
        val state =
            params.firstOrNull { it.first == "state" }?.second?.ifEmpty { null }
                ?: text.substringAfter('#', missingDelimiterValue = "").ifEmpty { null }
        require(code.isNotEmpty()) { "That link has no sign-in code." }
        require(state == null || state == attempt.state) { "That link is for a different sign-in." }
        return code
    }
    if ('#' in text) {
        val code = text.substringBefore('#')
        require(code.isNotEmpty() && text.substringAfter('#') == attempt.state) {
            "That code is for a different sign-in."
        }
        return code
    }
    return text
}

private fun urlDecode(value: String): String = runCatching {
    java.net.URLDecoder.decode(value, "UTF-8")
}
    .getOrDefault(value)

fun exchangeTokens(
    http: OkHttpClient,
    provider: OAuthProvider,
    attempt: OAuthAttempt,
    code: String,
    nowMillis: () -> Long = System::currentTimeMillis,
    tokenUrl: String = tokenUrl(provider),
): OAuthTokens {
    val body =
        if (provider == OAuthProvider.Claude) {
            buildJsonObject {
                    put("grant_type", JsonPrimitive("authorization_code"))
                    put("code", JsonPrimitive(code))
                    put("redirect_uri", JsonPrimitive(oauthRedirect(provider)))
                    put("client_id", JsonPrimitive(CLAUDE_CLIENT_ID))
                    put("code_verifier", JsonPrimitive(attempt.verifier))
                    put("state", JsonPrimitive(attempt.state))
                }
                .toString()
                .toByteArray()
                .toRequestBody(JSON_MEDIA_TYPE)
        } else {
            FormBody.Builder()
                .add("grant_type", "authorization_code")
                .add("code", code)
                .add("redirect_uri", oauthRedirect(provider))
                .add("client_id", CHATGPT_CLIENT_ID)
                .add("code_verifier", attempt.verifier)
                .build()
        }
    val exchanged = requestTokens(http, tokenUrl, body, nowMillis)
    return if (provider == OAuthProvider.Claude) exchanged.copy(scope = CLAUDE_SCOPE) else exchanged
}

fun refreshTokens(
    http: OkHttpClient,
    provider: OAuthProvider,
    tokens: OAuthTokens,
    nowMillis: () -> Long = System::currentTimeMillis,
    tokenUrl: String = tokenUrl(provider),
): OAuthTokens {
    val body = buildJsonObject {
        put("grant_type", JsonPrimitive("refresh_token"))
        put("refresh_token", JsonPrimitive(tokens.refreshToken))
        put(
            "client_id",
            JsonPrimitive(
                if (provider == OAuthProvider.Claude) CLAUDE_CLIENT_ID else CHATGPT_CLIENT_ID
            ),
        )
        if (provider == OAuthProvider.Claude)
            put("scope", JsonPrimitive(tokens.scope.ifEmpty { CLAUDE_SCOPE }))
        else put("scope", JsonPrimitive("openid profile email"))
    }
        .toString()
        .toByteArray()
        .toRequestBody(JSON_MEDIA_TYPE)
    val refreshed = requestTokens(http, tokenUrl, body, nowMillis)
    return refreshed.copy(
        refreshToken = refreshed.refreshToken.ifEmpty { tokens.refreshToken },
        idToken = refreshed.idToken.ifEmpty { tokens.idToken },
        scope = tokens.scope,
    )
}

private fun requestTokens(
    http: OkHttpClient,
    url: String,
    body: okhttp3.RequestBody,
    nowMillis: () -> Long,
): OAuthTokens {
    val request = Request.Builder().url(url).post(body).anthropicAxiosHeaders().build()
    http.newCall(request).execute().use { response ->
        if (!response.isSuccessful) throw OAuthHttpException(response.code)
        val json =
            Json.parseToJsonElement(response.body.string()) as? JsonObject
                ?: error("OAuth token endpoint returned invalid JSON")
        val access = json.string("access_token")
        if (access.isEmpty()) error("OAuth token endpoint returned no access token")
        val expiresIn = json.string("expires_in").toLongOrNull() ?: 3600L
        return OAuthTokens(
            access,
            json.string("refresh_token"),
            json.string("id_token"),
            nowMillis() + expiresIn * 1000,
            json.string("scope"),
        )
    }
}

/**
 * The header set Claude Code's Axios OAuth client sends, which CLI Proxy API replays to clear
 * Cloudflare's bot checks on Anthropic domains. Accept-Encoding is left to the shared client's
 * brotli interceptor, which advertises br/gzip and, crucially, decodes the reply; setting it here
 * ourselves would make the interceptor pass the compressed bytes through undecoded. OkHttp owns the
 * TLS handshake, so the JA3 fingerprint is Conscrypt's, not Node's.
 */
private fun Request.Builder.anthropicAxiosHeaders(): Request.Builder =
    header("Accept", "application/json, text/plain, */*")
        .header("User-Agent", "axios/1.15.2")
        .header("Connection", "close")

private val JSON_MEDIA_TYPE = "application/json".toMediaType()

private fun ByteArray.base64Url(): String =
    Base64.getUrlEncoder().withoutPadding().encodeToString(this)

private fun JsonObject.string(key: String): String = this[key]?.jsonPrimitive?.content.orEmpty()

private fun tokenUrl(provider: OAuthProvider): String =
    if (provider == OAuthProvider.Claude) "https://platform.claude.com/v1/oauth/token"
    else "https://auth.openai.com/oauth/token"
