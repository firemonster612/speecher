package app.speecher.android.auth

import app.speecher.protocol.OAuthProvider
import app.speecher.protocol.OAuthTokens
import app.speecher.protocol.ProviderFailure
import app.speecher.protocol.ProviderFailureKind
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.Future
import java.util.concurrent.FutureTask
import java.util.concurrent.Semaphore
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
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

@RunWith(RobolectricTestRunner::class)
class TokenRefreshTest {
    private val app = RuntimeEnvironment.getApplication()
    // Robolectric has no Android Keystore, so every store here shares a plain AES key.
    private val key = KeyGenerator.getInstance("AES").generateKey()
    private val expired = OAuthTokens("old access", "old refresh", "id", 0, "scope")
    private val requests = AtomicInteger()
    /** A permit for each request the token endpoint received. */
    private val requested = Semaphore(0)
    private val threads = Executors.newCachedThreadPool()

    @After
    fun stopThreads() {
        threads.shutdownNow()
    }

    /** A token endpoint that answers [status] once [answer] opens, with new tokens on a 200. */
    private fun tokenEndpoint(answer: CountDownLatch, status: Int = 200) =
        OkHttpClient.Builder()
            .addInterceptor { chain ->
                requests.incrementAndGet()
                requested.release()
                answer.await(5, TimeUnit.SECONDS)
                Response.Builder()
                    .request(chain.request())
                    .protocol(Protocol.HTTP_1_1)
                    .code(status)
                    .message("")
                    .body("""{"access_token":"new access","expires_in":3600}""".toResponseBody())
                    .build()
            }
            .build()

    /** Refreshes the saved Claude sign-in on another thread, its request held until [answer]. */
    private fun refresh(store: TokenStore, answer: CountDownLatch): Future<OAuthTokens?> =
        threads.submit<OAuthTokens?> {
            store.validTokens(OAuthProvider.Claude, tokenEndpoint(answer))
        }

    private fun store() = TokenStore(app, lazyOf(key))

    /** Waits for the token endpoint's next request. */
    private fun awaitRequest() = assertTrue(requested.tryAcquire(5, TimeUnit.SECONDS))

    @Test
    fun `a refresh that ends after a sign-out or a new sign-in keeps that change`() {
        val store = store()
        store.save(OAuthProvider.Claude, expired)
        val signOut = CountDownLatch(1)
        val signedOut = refresh(store, signOut)
        awaitRequest()
        store.signOut(OAuthProvider.Claude)
        signOut.countDown()
        assertNull(signedOut.get(5, TimeUnit.SECONDS))
        assertNull(store.load(OAuthProvider.Claude))

        store.save(OAuthProvider.Claude, expired)
        val signIn = CountDownLatch(1)
        val replaced = refresh(store, signIn)
        awaitRequest()
        val newer = OAuthTokens("newer access", "newer refresh", "id", Long.MAX_VALUE, "scope")
        store.save(OAuthProvider.Claude, newer)
        signIn.countDown()
        assertEquals(newer, replaced.get(5, TimeUnit.SECONDS))
        assertEquals(newer, store.load(OAuthProvider.Claude))
    }

    @Test
    fun `stores refresh a provider one at a time, so its refresh token is spent once`() {
        store().save(OAuthProvider.Claude, expired)
        val answer = CountDownLatch(1)
        val first = refresh(store(), answer)
        awaitRequest()
        val second = FutureTask { store().validTokens(OAuthProvider.Claude, tokenEndpoint(answer)) }
        val worker = Thread(second).apply { start() }
        // Waiting on the first store's refresh, rather than not started yet.
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5)
        while (worker.state != Thread.State.BLOCKED) {
            assertTrue(System.nanoTime() < deadline)
            Thread.sleep(10)
        }
        answer.countDown()
        assertEquals("new access", first.get(5, TimeUnit.SECONDS)?.accessToken)
        assertEquals("new access", second.get(5, TimeUnit.SECONDS)?.accessToken)
        assertEquals(1, requests.get())
    }

    @Test
    fun `a refresh turned down with 400 or 401 is a rejected sign-in`() {
        val store = store()
        val kinds =
            listOf(400, 401, 500).map { status ->
                store.save(OAuthProvider.ChatGpt, expired)
                try {
                    store.validTokens(
                        OAuthProvider.ChatGpt,
                        tokenEndpoint(CountDownLatch(0), status),
                    )
                    null
                } catch (failure: ProviderFailure) {
                    failure.kind
                }
            }
        assertEquals(
            listOf(
                ProviderFailureKind.Authentication,
                ProviderFailureKind.Authentication,
                ProviderFailureKind.Server,
            ),
            kinds,
        )
    }
}
