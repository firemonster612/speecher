package app.speecher.android.dictation

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
}
