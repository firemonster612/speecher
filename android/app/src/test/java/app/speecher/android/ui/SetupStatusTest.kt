package app.speecher.android.ui

import app.speecher.android.provider.Provider
import app.speecher.android.provider.SpeechProvider
import app.speecher.android.settings.SpeecherSettings
import app.speecher.protocol.SpeechEndpoint
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SetupStatusTest {
    @Test
    fun `one signed-in provider completes setup`() {
        val setup = SetupStatus(setOf(Provider.Claude), true, true, true)
        assertTrue(setup.complete)
        assertFalse(setup.copy(signedIn = emptySet()).complete)
    }

    @Test
    fun `a speech server of your own completes setup without an account`() {
        val server =
            SpeecherSettings(
                transcriptionProvider = SpeechProvider.Endpoint,
                speechEndpoint = SpeechEndpoint("http://10.0.2.2:8080/"),
            )
        assertTrue(server.transcribesWithServer)
        assertFalse(server.copy(speechEndpoint = SpeechEndpoint(" / ")).transcribesWithServer)
        assertTrue(SetupStatus(emptySet(), true, true, true, ownServer = true).complete)
    }
}
