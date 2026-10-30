package app.speecher.android.dictation

import app.speecher.android.provider.SpeechProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class SpokenLanguageTest {
    @Test
    fun `each service offers its own languages`() {
        assertTrue("yue" in SpeechProvider.Claude.spokenLanguages)
        assertFalse("cy" in SpeechProvider.Claude.spokenLanguages)
        assertTrue("cy" in SpeechProvider.ChatGpt.spokenLanguages)
        assertTrue("haw" in SpeechProvider.Endpoint.spokenLanguages)
    }

    @Test
    fun `languages show their English name, then the native one when it differs`() {
        assertEquals(
            listOf("Automatic", "English", "German (Deutsch)", "Japanese (日本語)", "Javanese (Jawa)"),
            listOf("auto", "en", "de", "ja", "jw").map(::spokenLanguageLabel),
        )
    }

    @Test
    fun `a language the service can't listen for is named, not replaced`() {
        assertEquals(
            "Claude can't listen for Welsh. Choose another spoken language.",
            spokenLanguageMismatch(SpeechProvider.Claude, "cy")?.advice,
        )
        assertNull(spokenLanguageMismatch(SpeechProvider.ChatGpt, "cy"))
    }
}
