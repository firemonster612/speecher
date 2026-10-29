package app.speecher.android.dictation

import app.speecher.android.provider.CleanupProvider
import app.speecher.android.provider.Provider
import app.speecher.android.provider.SpeechProvider
import app.speecher.protocol.ProviderFailureKind
import org.junit.Assert.assertEquals
import org.junit.Test

class RecoveryTest {
    @Test
    fun `each failure retries in place or opens the app where its fix is`() {
        val auth = ProviderFailureKind.Authentication
        val expected =
            listOf(
                FailureReason.MicrophoneUnavailable to Recovery.Retry,
                FailureReason.Commit to Recovery.Retry,
                FailureReason.SelectionChanged to Recovery.Retry,
                FailureReason.Speech(SpeechProvider.Claude, ProviderFailureKind.Network) to
                    Recovery.Retry,
                FailureReason.MicrophoneDenied to Recovery.AllowMicrophone,
                FailureReason.SpokenLanguage(SpeechProvider.Claude, "cy") to
                    Recovery.ChooseSpokenLanguage,
                FailureReason.SelectionNeedsCleanup to Recovery.ChooseCleanup,
                FailureReason.Cleanup(CleanupProvider.Claude, ProviderFailureKind.Unavailable) to
                    Recovery.SignIn(Provider.Claude),
                FailureReason.Speech(SpeechProvider.ChatGpt, auth) to
                    Recovery.SignIn(Provider.ChatGpt),
                FailureReason.Speech(SpeechProvider.Endpoint, auth) to Recovery.CheckSpeechServer,
            )
        assertEquals(expected, expected.map { (reason, _) -> reason to reason.recovery })
    }
}
