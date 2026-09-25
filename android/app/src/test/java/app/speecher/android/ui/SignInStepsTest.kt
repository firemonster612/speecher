package app.speecher.android.ui

import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
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
    fun `the browser opens only after the steps, fallback included, are acknowledged`() {
        var opened = 0
        compose.setContent { SignInSteps(Provider.Claude, { opened++ }, {}) }
        compose.onNodeWithText("http://localhost", substring = true).assertExists()
        val open = compose.onNodeWithText("Open Claude sign-in")
        open.assertIsNotEnabled().performClick()
        assertEquals(0, opened)
        compose.onNodeWithText("I understand").performClick()
        open.assertIsEnabled().performClick()
        assertEquals(1, opened)
    }
}
