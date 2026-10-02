package app.speecher.android.ui

import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import app.speecher.android.dictation.Provider
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class SignInStepsTest {
    @get:Rule val compose = createComposeRule()

    @Test
    fun `the browser opens on the first tap, with the paste fallback on screen`() {
        var opened = 0
        compose.setContent { SignInSteps(Provider.Claude, { opened++ }, {}) }
        compose.onNodeWithText("http://localhost", substring = true).assertExists()
        compose.onNodeWithText("Open Claude sign-in").performClick()
        assertEquals(1, opened)
    }
}
