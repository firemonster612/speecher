package app.speecher.android

import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.lifecycle.ViewModelProvider
import app.speecher.android.ui.ComputerImportViewModel
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class MainActivityTest {
    @get:Rule val compose = createAndroidComposeRule<MainActivity>()

    @Test
    fun `a scan that finishes after the activity is recreated shows its import page`() {
        compose.activityRule.scenario.recreate()
        compose.activityRule.scenario.onActivity {
            ViewModelProvider(it)[ComputerImportViewModel::class.java].importFrom("not a code")
        }
        compose.onNodeWithText("That isn't a Speecher code").assertExists()
    }
}
