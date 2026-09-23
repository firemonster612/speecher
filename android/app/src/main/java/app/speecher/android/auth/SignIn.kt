package app.speecher.android.auth

import android.app.Activity
import android.os.Handler
import android.os.Looper
import androidx.browser.customtabs.CustomTabsIntent
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.net.toUri
import androidx.lifecycle.ViewModel
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.oauth
import app.speecher.android.dictation.sharedExecutor
import app.speecher.android.dictation.sharedHttp
import app.speecher.protocol.OAuthAttempt
import app.speecher.protocol.OAuthProvider
import app.speecher.protocol.OAuthTokens
import app.speecher.protocol.exchangeTokens
import app.speecher.protocol.oauthAttempt
import app.speecher.protocol.oauthCallback
import app.speecher.protocol.pastedClaudeCode
import java.net.InetAddress
import java.net.ServerSocket
import java.net.SocketTimeoutException

/** Owns one browser sign-in attempt across activity recreation. */
class SignIn(private val tokenStore: TokenStore) : AutoCloseable {
    private val main = Handler(Looper.getMainLooper())
    @Volatile private var listener: ServerSocket? = null
    private var attempt: OAuthAttempt? = null
    private var provider: OAuthProvider? = null
    @Volatile private var sequence = 0

    fun start(activity: Activity, provider: OAuthProvider, result: (Result<OAuthTokens>) -> Unit) {
        closeListener()
        val id = ++sequence
        val current = oauthAttempt(provider)
        this.provider = provider
        attempt = current
        sharedExecutor.execute {
            if (id != sequence) return@execute
            val outcome = runCatching {
                val port = if (provider == OAuthProvider.Claude) 54545 else 1455
                val deadline = System.currentTimeMillis() + 180_000
                ServerSocket(port, 1, InetAddress.getLoopbackAddress()).use { socket ->
                    synchronized(this) {
                        if (id != sequence) {
                            socket.close()
                            error("Sign-in attempt was replaced")
                        }
                        listener = socket
                    }
                    main.post {
                        if (id == sequence)
                            CustomTabsIntent.Builder()
                                .build()
                                .launchUrl(activity, current.authorizeUrl.toString().toUri())
                    }
                    while (System.currentTimeMillis() < deadline && !socket.isClosed) {
                        socket.soTimeout =
                            minOf(2_000L, deadline - System.currentTimeMillis())
                                .toInt()
                                .coerceAtLeast(1)
                        val client =
                            try {
                                socket.accept()
                            } catch (_: SocketTimeoutException) {
                                continue
                            }
                        client.use {
                            it.soTimeout = 2_000
                            val requestLine =
                                try {
                                    it.getInputStream().bufferedReader().readLine()
                                } catch (_: SocketTimeoutException) {
                                    null
                                }
                            val target = requestLine?.split(' ')?.getOrNull(1)
                            val uri = runCatching { target?.toUri() }.getOrNull()
                            val code = runCatching {
                                oauthCallback(
                                    provider,
                                    current,
                                    uri?.path.orEmpty(),
                                    uri?.encodedQuery.orEmpty(),
                                )
                            }
                                .getOrNull()
                            if (code == null) {
                                respond(it, false)
                                continue
                            }
                            val exchange = runCatching { exchange(provider, current, code) }
                            respond(it, exchange.isSuccess)
                            return@runCatching exchange.getOrThrow()
                        }
                    }
                    error("Sign-in callback timed out")
                }
            }
            main.post {
                if (id == sequence) {
                    listener = null
                    result(outcome)
                }
            }
        }
    }

    fun completePastedClaudeCode(codeAndState: String, result: (Result<OAuthTokens>) -> Unit) {
        val current = requireNotNull(attempt) { "No sign-in in progress" }
        check(provider == OAuthProvider.Claude) { "Paste code is for Claude only" }
        ++sequence
        closeListener()
        sharedExecutor.execute {
            val outcome = runCatching {
                exchange(OAuthProvider.Claude, current, pastedClaudeCode(current, codeAndState))
            }
            main.post { result(outcome) }
        }
    }

    private fun exchange(
        provider: OAuthProvider,
        attempt: OAuthAttempt,
        code: String,
    ): OAuthTokens {
        val saved = exchangeTokens(sharedHttp, provider, attempt, code)
        tokenStore.save(provider, saved)
        return saved
    }

    private fun respond(client: java.net.Socket, success: Boolean) {
        val status = if (success) "200 OK" else "400 Bad Request"
        val message =
            if (success) "You can return to Speecher."
            else "Sign-in failed. Return to Speecher and try again."
        client
            .getOutputStream()
            .write(
                "HTTP/1.1 $status\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n$message"
                    .toByteArray()
            )
    }

    @Synchronized
    private fun closeListener() {
        listener?.close()
        listener = null
    }

    override fun close() {
        ++sequence
        closeListener()
    }
}

class SignInViewModel : ViewModel() {
    private var signIn: SignIn? = null
    var activeProvider by mutableStateOf<Provider?>(null)
        private set

    var error by mutableStateOf<String?>(null)
        private set

    var completed by mutableIntStateOf(0)
        private set

    fun start(activity: Activity, provider: Provider) {
        if (signIn == null) signIn = SignIn(TokenStore(activity.applicationContext))
        activeProvider = provider
        error = null
        signIn?.start(activity, provider.oauth, ::finish)
    }

    fun paste(codeAndState: String) {
        signIn?.completePastedClaudeCode(codeAndState, ::finish)
    }

    private fun finish(result: Result<OAuthTokens>) {
        error = result.exceptionOrNull()?.message?.let { "Sign-in failed: $it" }
        activeProvider = null
        completed++
    }

    override fun onCleared() {
        signIn?.close()
    }
}
