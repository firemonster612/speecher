package app.speecher.android.auth

import android.app.Activity
import android.content.Context
import android.os.Handler
import android.os.Looper
import androidx.browser.customtabs.CustomTabsIntent
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.content.edit
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
import app.speecher.protocol.pastedCode
import app.speecher.protocol.restoredAttempt
import java.net.ServerSocket
import java.net.SocketTimeoutException
import java.util.concurrent.CountDownLatch

/**
 * Owns one browser sign-in attempt, persisted so a pasted callback survives the app being killed.
 */
class SignIn(context: Context) : AutoCloseable {
    private val tokenStore = TokenStore(context)
    private val pending = context.getSharedPreferences("pending-signin", Context.MODE_PRIVATE)
    private val main = Handler(Looper.getMainLooper())
    @Volatile private var listener: ServerSocket? = null
    private var listenerClosed: CountDownLatch? = null
    private var attempt: OAuthAttempt? = restoredPending()
    private var provider: OAuthProvider? = restoredProvider()
    @Volatile private var sequence = 0

    /** The provider of a sign-in still waiting for its code, restored after the app was killed. */
    val pendingProvider: OAuthProvider?
        get() = provider

    fun start(provider: OAuthProvider, result: (Result<OAuthTokens>) -> Unit): OAuthAttempt {
        closeListener()
        val previous = listenerClosed
        val closed = CountDownLatch(1)
        listenerClosed = closed
        val id = ++sequence
        val current = oauthAttempt(provider)
        this.provider = provider
        attempt = current
        pending.edit(commit = true) {
            putString("provider", provider.name)
            putString("verifier", current.verifier)
            putString("state", current.state)
        }
        sharedExecutor.execute {
            try {
                previous?.await()
                if (id != sequence) return@execute
                val outcome = runCatching {
                    val port = if (provider == OAuthProvider.Claude) 54545 else 1455
                    val deadline = System.currentTimeMillis() + 180_000
                    // Wildcard bind so the browser reaches us on either 127.0.0.1 or ::1; the
                    // accept
                    // loop rejects anything that is not from this device's loopback.
                    ServerSocket(port, 1).use { socket ->
                        synchronized(this) {
                            if (id != sequence) {
                                socket.close()
                                error("Sign-in attempt was replaced")
                            }
                            listener = socket
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
                                if (!it.inetAddress.isLoopbackAddress) return@use
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
            } finally {
                closed.countDown()
            }
        }
        return current
    }

    fun completePastedCode(pasted: String, result: (Result<OAuthTokens>) -> Unit) {
        val current = requireNotNull(attempt) { "No sign-in in progress. Tap Sign in first." }
        val forProvider = requireNotNull(provider) { "No sign-in in progress. Tap Sign in first." }
        ++sequence
        closeListener()
        sharedExecutor.execute {
            val outcome = runCatching {
                exchange(forProvider, current, pastedCode(current, pasted))
            }
            main.post { result(outcome) }
        }
    }

    private fun restoredPending(): OAuthAttempt? {
        val verifier = pending.getString("verifier", null) ?: return null
        val state = pending.getString("state", null) ?: return null
        return restoredAttempt(verifier, state)
    }

    private fun restoredProvider(): OAuthProvider? =
        pending.getString("provider", null)?.let { name ->
            OAuthProvider.entries.firstOrNull { it.name == name }
        }

    private fun exchange(
        provider: OAuthProvider,
        attempt: OAuthAttempt,
        code: String,
    ): OAuthTokens {
        val saved = exchangeTokens(sharedHttp, provider, attempt, code)
        tokenStore.save(provider, saved)
        pending.edit(commit = true) { clear() }
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

    fun start(activity: Activity, provider: Provider) {
        if (signIn == null) signIn = SignIn(activity.applicationContext)
        activeProvider = provider
        error = null
        val attempt = signIn?.start(provider.oauth, ::finish) ?: return
        CustomTabsIntent.Builder()
            .build()
            .launchUrl(activity, attempt.authorizeUrl.toString().toUri())
    }

    /**
     * After the app was killed mid-sign-in, bring back the paste field for the pending provider.
     */
    fun restore(activity: Activity) {
        val existing = signIn ?: SignIn(activity.applicationContext).also { signIn = it }
        if (activeProvider == null) {
            activeProvider =
                existing.pendingProvider?.let { pending ->
                    Provider.entries.firstOrNull { it.oauth == pending }
                }
        }
    }

    fun paste(pasted: String) {
        signIn?.completePastedCode(pasted, ::finish)
    }

    private fun finish(result: Result<OAuthTokens>) {
        error = result.exceptionOrNull()?.let(::signInErrorMessage)
        activeProvider = null
    }

    // Some failures (dropped sockets, cancellations) carry no message; never leave the user with
    // no feedback, and turn the few known technical strings into plain copy.
    private fun signInErrorMessage(cause: Throwable): String {
        val message = cause.message?.trim()
        return when {
            message.isNullOrEmpty() -> "Sign-in didn't complete. Try again."
            message.contains("Address already in use", ignoreCase = true) ->
                "Couldn't start sign-in — another attempt may still be open. Try again in a moment."
            message.contains("timed out", ignoreCase = true) -> "Sign-in timed out. Try again."
            else -> "Sign-in failed: $message"
        }
    }

    override fun onCleared() {
        signIn?.close()
    }
}
