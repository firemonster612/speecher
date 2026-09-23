package app.speecher.android

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import app.speecher.android.auth.TokenStore
import app.speecher.protocol.OAuthProvider
import app.speecher.protocol.OAuthTokens

/**
 * Debug builds only. Stores a fake, far-future Claude token so emulator tests can run the engine
 * against the local fake speech server: `am broadcast -n app.speecher.android/.FakeSignInReceiver`.
 */
class FakeSignInReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        TokenStore(context)
            .save(
                OAuthProvider.Claude,
                OAuthTokens("fake-access", "fake-refresh", "", Long.MAX_VALUE, ""),
            )
    }
}
