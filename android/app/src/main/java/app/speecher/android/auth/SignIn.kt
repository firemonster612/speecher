package app.speecher.android.auth

import android.app.Activity
import android.app.Application
import android.content.Context
import android.os.Handler
import android.os.Looper
import androidx.browser.customtabs.CustomTabsIntent
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.content.edit
import androidx.core.net.toUri
import androidx.lifecycle.AndroidViewModel
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.oauth
import app.speecher.android.dictation.sharedExecutor
import app.speecher.android.dictation.sharedHttp
import app.speecher.android.ui.label
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
                                    respond(it, provider, false)
                                    continue
                                }
                                val exchange = runCatching { exchange(provider, current, code) }
                                respond(it, provider, exchange.isSuccess)
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

    private fun respond(client: java.net.Socket, provider: OAuthProvider, success: Boolean) {
        val status = if (success) "200 OK" else "400 Bad Request"
        val label = Provider.entries.first { it.oauth == provider }.label
        client
            .getOutputStream()
            .write(
                ("HTTP/1.1 $status\r\nContent-Type: text/html; charset=utf-8\r\n" +
                        "Connection: close\r\n\r\n${signInPage(label, success)}")
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

/**
 * The page the browser shows after the redirect to Speecher's loopback listener. Custom Tabs
 * usually ignore `window.close()`, so the page also says how to leave.
 */
internal fun signInPage(provider: String, success: Boolean): String {
    val message =
        if (success)
            "<h1>Signed in to $provider</h1><p>You can close this tab. It closes in 5 seconds.</p>" +
                "<p>Tap the X in the top-left corner to go back to Speecher.</p>" +
                "<script>setTimeout(() => window.close(), 5000)</script>"
        else "<h1>Sign-in didn't complete</h1><p>Go back to Speecher and try again.</p>"
    // The app icon's geometry, from packaging/io.github.firemonster612.speecher.svg.
    val mark =
        """<svg viewBox="0 0 128 128" width="72" height="72" aria-hidden="true">""" +
            """<rect width="128" height="128" rx="28" fill="#1f1f1f"/>""" +
            """<rect x="26" y="36" width="76" height="56" rx="28" fill="#f2f0e6"/>""" +
            """<rect x="43" y="52" width="7" height="24" rx="3.5" fill="#202020"/>""" +
            """<rect x="56" y="44" width="7" height="40" rx="3.5" fill="#202020"/>""" +
            """<rect x="69" y="48" width="7" height="32" rx="3.5" fill="#202020"/>""" +
            """<rect x="82" y="56" width="7" height="16" rx="3.5" fill="#202020"/></svg>"""
    return """<!doctype html><html><head><meta charset="utf-8">""" +
        """<meta name="viewport" content="width=device-width, initial-scale=1">""" +
        "<title>Speecher</title><style>" +
        ":root{color-scheme:light dark}" +
        "body{margin:0;padding:48px 24px;font:18px/1.5 system-ui,sans-serif;text-align:center;" +
        "background:#faf9f4;color:#1c1b18}" +
        "h1{font-size:28px;line-height:1.25;margin:24px 0 16px}p{margin:8px 0;color:#5f5c53}" +
        "@media (prefers-color-scheme:dark){body{background:#131312;color:#e6e4da}" +
        "p{color:#bab7ab}}" +
        "</style></head><body>$mark$message</body></html>"
}

/**
 * The sign-in attempt the screens show. While one waits in the browser, [SignInListenerService]
 * keeps the process from being frozen; every way the attempt ends stops it.
 */
class SignInViewModel(application: Application) : AndroidViewModel(application) {
    private val signIn = SignIn(application)
    var activeProvider by mutableStateOf<Provider?>(null)
        private set

    var error by mutableStateOf<String?>(null)
        private set

    fun start(activity: Activity, provider: Provider) {
        activeProvider = provider
        error = null
        // Before the browser covers us: Android only lets a foreground app start the service.
        SignInListenerService.start(activity, provider, ::timedOut)
        val attempt = signIn.start(provider.oauth, ::finish)
        CustomTabsIntent.Builder()
            .build()
            .launchUrl(activity, attempt.authorizeUrl.toString().toUri())
    }

    /**
     * After the app was killed mid-sign-in, bring back the paste field for the pending provider.
     */
    fun restore() {
        if (activeProvider == null) {
            activeProvider =
                signIn.pendingProvider?.let { pending ->
                    Provider.entries.firstOrNull { it.oauth == pending }
                }
        }
    }

    fun paste(pasted: String) {
        signIn.completePastedCode(pasted, ::finish)
    }

    private fun timedOut() {
        signIn.close()
        finish(Result.failure(IllegalStateException("Sign-in callback timed out")))
    }

    private fun finish(result: Result<OAuthTokens>) {
        SignInListenerService.stop(getApplication())
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
        signIn.close()
        SignInListenerService.stop(getApplication())
    }
}
