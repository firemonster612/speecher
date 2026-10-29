package app.speecher.android

import android.content.Intent
import android.os.Parcel
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.junit4.v2.createEmptyComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.lifecycle.ViewModelProvider
import androidx.test.core.app.ActivityScenario
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.Recovery
import app.speecher.android.ui.ComputerImportViewModel
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

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

/** A panel failure's recovery, delivered the way the IME sends it. */
@RunWith(RobolectricTestRunner::class)
class MainActivityRecoveryTest {
    @get:Rule val compose = createEmptyComposeRule()

    // Extras cross processes as a Parcel, so the recovery is read back from one.
    private fun open(recovery: Recovery): ActivityScenario<MainActivity> {
        val sent =
            Intent(RuntimeEnvironment.getApplication(), MainActivity::class.java)
                .putExtra(RECOVERY, recovery)
        val parcel = Parcel.obtain()
        sent.writeToParcel(parcel, 0)
        parcel.setDataPosition(0)
        val received = Intent.CREATOR.createFromParcel(parcel)
        parcel.recycle()
        return ActivityScenario.launch(received)
    }

    @Test
    fun `a spoken language recovery opens the Transcription settings`() {
        open(Recovery.ChooseSpokenLanguage).use {
            compose.onNodeWithText("The language you dictate in.").assertExists()
        }
    }

    @Test
    fun `a sign-in recovery starts that account's sign-in`() {
        open(Recovery.SignIn(Provider.ChatGpt)).use {
            compose
                .onNodeWithText("A browser opens to ChatGPT's sign-in page", substring = true)
                .assertExists()
        }
    }
}
