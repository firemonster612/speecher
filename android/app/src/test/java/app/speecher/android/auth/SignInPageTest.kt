package app.speecher.android.auth

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SignInPageTest {
    @Test
    fun `the success page names the provider and says how to leave the tab`() {
        val page = signInPage("ChatGPT", success = true)
        assertTrue(page.contains("Signed in to ChatGPT"))
        assertTrue(page.contains("Tap the X in the top-left corner to go back to Speecher."))
    }

    @Test
    fun `the failure page says to try again and never claims success`() {
        val page = signInPage("Claude", success = false)
        assertTrue(page.contains("Go back to Speecher and try again."))
        assertFalse(page.contains("Signed in"))
    }
}
