package app.speecher.android.auth

import app.speecher.android.provider.Provider
import app.speecher.protocol.OAuthProvider
import app.speecher.protocol.OAuthTokens
import javax.crypto.KeyGenerator
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

@RunWith(RobolectricTestRunner::class)
class SessionEndedTest {
    private val app = RuntimeEnvironment.getApplication()
    // Robolectric has no Android Keystore.
    private val key = KeyGenerator.getInstance("AES").generateKey()
    private val tokens = TokenStore(app, lazyOf(key))
    private val rejected = OAuthTokens("access", "refresh", "id", Long.MAX_VALUE, "scope")

    @Test
    fun `an ended session is remembered per provider until sign-out`() {
        tokens.save(OAuthProvider.Claude, rejected)
        tokens.endSession(OAuthProvider.Claude, rejected)
        assertEquals(setOf(Provider.Claude), tokens.sessionEnded())
        tokens.signOut(OAuthProvider.Claude)
        assertEquals(emptySet<Provider>(), tokens.sessionEnded())
    }

    @Test
    fun `a sign-in saved after the rejected one keeps its session`() {
        tokens.save(OAuthProvider.Claude, rejected)
        tokens.save(OAuthProvider.Claude, rejected.copy(accessToken = "newer access"))
        tokens.endSession(OAuthProvider.Claude, rejected)
        assertEquals(emptySet<Provider>(), tokens.sessionEnded())
    }

    @Test
    fun `the signed-in accounts are listed without decrypting a sign-in`() {
        tokens.save(OAuthProvider.Claude, rejected)
        val withoutKeystore = TokenStore(app, lazy { error("Keystore read") })
        assertEquals(setOf(Provider.Claude), withoutKeystore.signedIn())
    }
}
