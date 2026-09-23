package app.speecher.android.dictation

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
    fun `dictation falls back to the signed-in provider`() {
        // Prefer Claude, but only ChatGPT is signed in: use ChatGPT rather than fail signed-out.
        assertEquals(
            Provider.ChatGpt,
            resolveSignedIn(Provider.Claude, setOf(Provider.ChatGpt)),
        )
        // Preferred provider is signed in: keep it.
        assertEquals(
            Provider.Claude,
            resolveSignedIn(Provider.Claude, setOf(Provider.Claude, Provider.ChatGpt)),
        )
    }
}
