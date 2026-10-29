package app.speecher.android.dictation

import app.speecher.android.ui.SettingsPage
import app.speecher.protocol.ProviderFailureKind
import org.junit.Assert.assertEquals
import org.junit.Test

class RecoveryTest {
    @Test
    fun `each failure retries in place or opens the app where its fix is`() {
        val auth = ProviderFailureKind.Authentication
        assertEquals(
            listOf(
                Recovery.Retry,
                Recovery.Retry,
                Recovery.Retry,
                Recovery.Retry,
                Recovery.OpenApp(null),
                Recovery.OpenApp(SettingsPage.Transcription),
                Recovery.OpenApp(SettingsPage.Profiles),
                Recovery.SignIn(Provider.Claude),
                Recovery.SignIn(Provider.ChatGpt),
                Recovery.OpenApp(SettingsPage.Transcription),
            ),
            listOf(
                    FailureReason.MicrophoneUnavailable,
                    FailureReason.Commit,
                    FailureReason.SelectionChanged,
                    FailureReason.Speech(SpeechProvider.Claude, ProviderFailureKind.Network),
                    FailureReason.MicrophoneDenied,
                    FailureReason.SpokenLanguage(SpeechProvider.Claude, "cy"),
                    FailureReason.SelectionNeedsCleanup,
                    FailureReason.Cleanup(CleanupProvider.Claude, ProviderFailureKind.Unavailable),
                    FailureReason.Speech(SpeechProvider.ChatGpt, auth),
                    FailureReason.Speech(SpeechProvider.Endpoint, auth),
                )
                .map { it.recovery },
        )
    }
}
