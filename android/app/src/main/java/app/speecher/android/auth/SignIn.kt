package app.speecher.android.auth

import android.app.Activity
import androidx.browser.customtabs.CustomTabsIntent
import androidx.core.net.toUri
import app.speecher.protocol.OAuthAttempt
import app.speecher.protocol.OAuthProvider
import app.speecher.protocol.OAuthTokenClient
import app.speecher.protocol.OAuthTokens
import app.speecher.protocol.oauthAttempt
import app.speecher.protocol.oauthCallback
import app.speecher.protocol.pastedClaudeCode
import java.net.InetAddress
import java.net.ServerSocket
import java.util.concurrent.Executors
import okhttp3.OkHttpClient

/** A sign-in attempt lives until its loopback callback, a pasted Claude code, or [close]. */
class SignIn(private val activity: Activity, private val tokenStore: TokenStore) : AutoCloseable {
    private val executor = Executors.newFixedThreadPool(2)
    private val tokens = OAuthTokenClient(OkHttpClient())
    private var listener: ServerSocket? = null
    private var attempt: OAuthAttempt? = null
    private var provider: OAuthProvider? = null

    fun start(provider: OAuthProvider, result: (Result<OAuthTokens>) -> Unit) {
        closeListener()
        val current = oauthAttempt(provider)
        val port = if (provider == OAuthProvider.Claude) 54545 else 1455
        val socket = ServerSocket(port, 1, InetAddress.getLoopbackAddress())
        this.provider = provider
        attempt = current
        listener = socket
        executor.execute {
            val outcome = runCatching {
                socket.accept().use { client ->
                    val requestLine = client.getInputStream().bufferedReader().readLine()
                    val target = requestLine.split(' ')[1]
                    val uri = target.toUri()
                    val code =
                        oauthCallback(
                            provider,
                            current,
                            uri.path.orEmpty(),
                            uri.encodedQuery.orEmpty(),
                        )
                    client
                        .getOutputStream()
                        .write(
                            "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nYou can return to Speecher."
                                .toByteArray(Charsets.UTF_8)
                        )
                    exchange(provider, current, code)
                }
            }
            if (!(socket.isClosed && outcome.isFailure)) activity.runOnUiThread { result(outcome) }
        }
        CustomTabsIntent.Builder()
            .build()
            .launchUrl(activity, current.authorizeUrl.toString().toUri())
    }

    fun completePastedClaudeCode(codeAndState: String, result: (Result<OAuthTokens>) -> Unit) {
        val current = requireNotNull(attempt) { "No sign-in in progress" }
        check(provider == OAuthProvider.Claude) { "Paste code is for Claude only" }
        closeListener()
        executor.execute {
            val outcome = runCatching {
                exchange(OAuthProvider.Claude, current, pastedClaudeCode(current, codeAndState))
            }
            activity.runOnUiThread { result(outcome) }
        }
    }

    private fun exchange(
        provider: OAuthProvider,
        attempt: OAuthAttempt,
        code: String,
    ): OAuthTokens {
        val saved = tokens.exchange(provider, attempt, code)
        tokenStore.save(provider, saved)
        closeListener()
        return saved
    }

    private fun closeListener() {
        listener?.close()
        listener = null
    }

    override fun close() {
        closeListener()
        executor.shutdownNow()
    }
}
