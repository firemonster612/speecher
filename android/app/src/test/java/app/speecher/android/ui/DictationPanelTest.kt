package app.speecher.android.ui

import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import app.speecher.android.dictation.ButtonLayout
import app.speecher.android.dictation.CleanupProvider
import app.speecher.android.dictation.DictationState
import app.speecher.android.dictation.FailureReason
import app.speecher.android.dictation.PanelSize
import app.speecher.android.dictation.SpeechProvider
import app.speecher.protocol.ProviderFailureKind
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class DictationPanelTest {
    @get:Rule val compose = createComposeRule()

    private fun show(reason: FailureReason, transcript: String = "") = compose.setContent {
        DictationPanel(
            DictationState.Failed(reason, "", transcript),
            ButtonLayout.RefinedPrimary,
            PanelSize.Full,
            {},
            {},
            {},
            {},
            {},
        )
    }

    @Test
    fun `a cleanup failure says cleanup failed and why, and keeps the words to insert`() {
        show(
            FailureReason.Cleanup(CleanupProvider.Claude, ProviderFailureKind.RateLimited),
            "heard words",
        )
        compose.onNodeWithText("Cleanup failed").assertExists()
        compose.onNodeWithText("Transcription failed").assertDoesNotExist()
        compose.onNodeWithText("Claude hit a usage limit. Try again later.").assertExists()
        compose.onNodeWithText("Insert").assertExists()
        compose.onNodeWithText("Retry").assertExists()
    }

    @Test
    fun `a rejected speech sign-in names the account and offers to sign in`() {
        show(FailureReason.Speech(SpeechProvider.ChatGpt, ProviderFailureKind.Authentication))
        compose.onNodeWithText("Transcription failed").assertExists()
        compose
            .onNodeWithText("Your ChatGPT sign-in has expired. Sign in again to keep dictating.")
            .assertExists()
        compose.onNodeWithText("Sign in").assertExists()
    }

    @Test
    fun `a server that turns down its key sends you to its settings, not to sign in`() {
        show(FailureReason.Speech(SpeechProvider.Endpoint, ProviderFailureKind.Authentication))
        compose
            .onNodeWithText("Custom Endpoint turned down its key. Check the key in Speecher.")
            .assertExists()
        compose.onNodeWithText("Open Speecher").assertExists()
    }
}
