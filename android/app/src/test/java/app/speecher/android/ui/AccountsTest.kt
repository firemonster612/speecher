package app.speecher.android.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SpeecherSettings
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class AccountsTest {
    @get:Rule val compose = createComposeRule()

    @Test
    fun `signing in again after a session ended offers the paste fallback`() {
        compose.setContent {
            Column {
                Settings(
                    SpeecherSettings(),
                    setOf(Provider.Claude),
                    {},
                    {},
                    {},
                    {},
                    sessionEnded = setOf(Provider.Claude),
                    signingIn = Provider.Claude,
                )
            }
        }
        compose.onNodeWithText("Pasted link or code").assertExists()
    }

    @Test
    fun `an ended session can still be signed out and is named as ended`() {
        var signedOut: Provider? = null
        compose.setContent {
            Column {
                Settings(
                    SpeecherSettings(transcriptionProvider = Provider.Claude),
                    setOf(Provider.Claude),
                    {},
                    {},
                    { signedOut = it },
                    {},
                    sessionEnded = setOf(Provider.Claude),
                )
            }
        }
        compose.onNodeWithText("Claude (session ended)").assertExists()
        compose.onNodeWithText("Sign in again").assertExists()
        compose.onNodeWithText("Sign out").performClick()
        assertEquals(Provider.Claude, signedOut)
    }
}
