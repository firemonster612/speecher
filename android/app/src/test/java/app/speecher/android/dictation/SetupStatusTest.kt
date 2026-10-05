package app.speecher.android.dictation

import app.speecher.protocol.SpeechEndpoint
import org.junit.Assert.assertEquals
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

    @Test
    fun `dictation falls back to the signed-in provider`() {
        // Prefer Claude, but only ChatGPT is signed in: use ChatGPT rather than fail signed-out.
        assertEquals(
            Provider.ChatGpt,
            resolveSignedIn(Provider.Claude, setOf(Provider.ChatGpt)),
        )
        // A server of your own needs no account, so it stays.
        assertEquals(
            SpeechProvider.Endpoint,
            resolveSpeech(SpeechProvider.Endpoint, setOf(Provider.ChatGpt)),
        )
        // Preferred provider is signed in: keep it.
        assertEquals(
            Provider.Claude,
            resolveSignedIn(Provider.Claude, setOf(Provider.Claude, Provider.ChatGpt)),
        )
    }

    @Test
    fun `provider order and default carry no Claude bias`() {
        // Neutral, alphabetical order, not the enum declaration order (Claude first).
        assertEquals(listOf(Provider.ChatGpt, Provider.Claude), providerOrder)
        // Default follows the signed-in account.
        assertEquals(Provider.Claude, defaultProvider(setOf(Provider.Claude)))
        assertEquals(Provider.ChatGpt, defaultProvider(setOf(Provider.ChatGpt)))
        // None signed in: the first neutral choice, not a hardcoded Claude.
        assertEquals(Provider.ChatGpt, defaultProvider(emptySet()))
    }
}
