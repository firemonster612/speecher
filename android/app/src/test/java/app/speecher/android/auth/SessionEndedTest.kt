package app.speecher.android.auth

import app.speecher.android.dictation.Provider
import app.speecher.protocol.OAuthProvider
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

@RunWith(RobolectricTestRunner::class)
class SessionEndedTest {
    @Test
    fun `an ended session is remembered per provider until sign-out`() {
        val tokens = TokenStore(RuntimeEnvironment.getApplication())
        tokens.endSession(OAuthProvider.Claude)
        assertEquals(setOf(Provider.Claude), tokens.sessionEnded())
        tokens.signOut(OAuthProvider.Claude)
        assertEquals(emptySet<Provider>(), tokens.sessionEnded())
    }
}
