package app.speecher.android.auth

import android.content.Context
import android.os.Looper
import androidx.core.content.edit
import app.speecher.protocol.OAuthProvider
import app.speecher.protocol.OAuthTokens
import java.net.ConnectException
import java.net.InetAddress
import java.net.Socket
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import javax.crypto.KeyGenerator
import okhttp3.OkHttpClient
import okhttp3.Protocol
import okhttp3.Response
import okhttp3.ResponseBody.Companion.toResponseBody
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
class SignInTest {
    private val app = RuntimeEnvironment.getApplication()
    private val pending = app.getSharedPreferences("pending-signin", Context.MODE_PRIVATE)
    // Robolectric has no Android Keystore.
    private val tokens = TokenStore(app, lazyOf(KeyGenerator.getInstance("AES").generateKey()))
    private val requests = AtomicInteger()
    /** Opens when the token endpoint may answer. */
    private val answer = CountDownLatch(1)
    private val tokenEndpoint =
        OkHttpClient.Builder()
            .addInterceptor { chain ->
                requests.incrementAndGet()
                answer.await(5, TimeUnit.SECONDS)
                Response.Builder()
                    .request(chain.request())
                    .protocol(Protocol.HTTP_1_1)
                    .code(200)
                    .message("")
                    .body("""{"access_token":"access","expires_in":3600}""".toResponseBody())
                    .build()
            }
            .build()
    private val results = mutableListOf<Result<OAuthTokens>>()
    private lateinit var signIn: SignIn

    /** A Claude attempt left waiting for its code by an earlier run of the app. */
    @Before
    fun restorePendingAttempt() {
        pending.edit(commit = true) {
            putString("provider", OAuthProvider.Claude.name)
            putString("verifier", "verifier")
            putString("state", "state")
        }
        signIn = SignIn(app, tokenEndpoint, tokens)
    }

    @After
    fun closeSignIn() {
        signIn.close()
        pending.edit(commit = true) { clear() }
    }

    /** Waits for the exchange to report back on the main thread, then runs that report. */
    private fun awaitExchange() {
        val main = shadowOf(Looper.getMainLooper())
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5)
        while (main.isIdle) {
            assertTrue(System.nanoTime() < deadline)
            Thread.sleep(10)
        }
        main.idle()
    }

    /** The browser's connection to the sign-in listener, once the listener is up. */
    private fun browser(): Socket {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5)
        while (true) {
            try {
                return Socket(InetAddress.getLoopbackAddress(), CLAUDE_LISTENER_PORT)
            } catch (_: ConnectException) {
                assertTrue(System.nanoTime() < deadline)
                Thread.sleep(10)
            }
        }
    }

    /** The browser's redirect to [path], which the sign-in page answers. */
    private fun Socket.redirect(path: String) =
        getOutputStream().write("GET $path HTTP/1.1\r\n\r\n".toByteArray())

    private fun awaitRequests(count: Int) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5)
        while (requests.get() < count) {
            assertTrue(System.nanoTime() < deadline)
            Thread.sleep(10)
        }
    }

    @Test
    fun `a code pasted twice is exchanged once`() {
        signIn.completePastedCode("code") { results += it }
        signIn.completePastedCode("code") { results += it }
        answer.countDown()
        awaitExchange()

        assertEquals(1, requests.get())
        assertEquals(listOf("access"), results.map { it.getOrThrow().accessToken })
    }

    @Test
    fun `a code pasted while the browser's code is exchanged is not exchanged again`() {
        val attempt = signIn.start(OAuthProvider.Claude, "Claude") { results += it }
        browser().use {
            it.redirect("/callback?code=browser&state=${attempt.state}")
            awaitRequests(1)
            signIn.completePastedCode("code") { results += it }
            answer.countDown()
            awaitExchange()
        }

        assertEquals(1, requests.get())
        assertEquals(listOf("access"), results.map { it.getOrThrow().accessToken })
        assertEquals("access", tokens.load(OAuthProvider.Claude)?.accessToken)
    }

    @Test
    fun `a browser callback for a replaced attempt leaves the newer attempt's exchange alone`() {
        answer.countDown()
        val stale = signIn.start(OAuthProvider.Claude, "Claude") {}
        browser().use { browser ->
            // Long enough for the stale attempt's listener to accept the browser before the
            // newer attempt closes it.
            Thread.sleep(ACCEPT_MILLIS)
            signIn.start(OAuthProvider.Claude, "Claude") {}
            signIn.completePastedCode("code") { results += it }
            awaitExchange()

            browser.redirect("/callback?code=stale&state=${stale.state}")
            assertEquals(
                "HTTP/1.1 400 Bad Request",
                browser.getInputStream().bufferedReader().readLine(),
            )
        }
        signIn.completePastedCode("code") { results += it }

        assertEquals(1, requests.get())
        assertEquals(1, results.size)
    }

    @Test
    fun `a pasted code exchanged after a newer attempt started leaves that attempt alone`() {
        signIn.completePastedCode("code") { results += it }
        val newer = signIn.start(OAuthProvider.Claude, "Claude") {}
        answer.countDown()
        awaitExchange()

        assertEquals(emptyList<Result<OAuthTokens>>(), results)
        assertNull(tokens.load(OAuthProvider.Claude))
        assertEquals(newer.state, pending.getString("state", null))
    }
}

/** The port Claude's sign-in redirect comes back to. */
private const val CLAUDE_LISTENER_PORT = 54545

private const val ACCEPT_MILLIS = 300L
