package app.speecher.android

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import app.speecher.android.auth.TokenStore
import app.speecher.protocol.OAuthProvider
import app.speecher.protocol.OAuthTokens

/**
 * Debug builds only. Stores a fake, far-future token so emulator tests can run the engine against
 * the local fake speech server. Defaults to Claude; pass `--es provider ChatGpt` for the other: `am
 * broadcast -n app.speecher.android/.FakeSignInReceiver --es provider ChatGpt`.
 */
class FakeSignInReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val provider =
            OAuthProvider.entries.firstOrNull { it.name == intent.getStringExtra("provider") }
                ?: OAuthProvider.Claude
        TokenStore(context)
            .save(provider, OAuthTokens("fake-access", "fake-refresh", "", Long.MAX_VALUE, ""))
    }
}
