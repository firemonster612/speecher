package app.speecher.android.ui

import android.Manifest
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.LocalActivityResultRegistryOwner
import androidx.activity.result.ActivityResultRegistry
import androidx.activity.result.ActivityResultRegistryOwner
import androidx.activity.result.contract.ActivityResultContract
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.core.app.ActivityOptionsCompat
import app.speecher.android.settings.SpeecherSettings
import app.speecher.protocol.SpeechEndpoint
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
class SpeechEndpointSettingsTest {
    @get:Rule val compose = createAndroidComposeRule<ComponentActivity>()

    private val asked = mutableListOf<Any?>()
    /** Android's prompt, refusing every permission it is asked for. */
    private val refusing =
        object : ActivityResultRegistryOwner {
            override val activityResultRegistry =
                object : ActivityResultRegistry() {
                    override fun <I, O> onLaunch(
                        requestCode: Int,
                        contract: ActivityResultContract<I, O>,
                        input: I,
                        options: ActivityOptionsCompat?,
                    ) {
                        asked.add(input)
                        dispatchResult(requestCode, false)
                    }
                }
        }

    @Test
    fun `testing a server on the local network asks to reach it, then opens settings once refused`() {
        val settings = SpeecherSettings(speechEndpoint = SpeechEndpoint("http://192.168.1.20:8080"))
        compose.setContent {
            CompositionLocalProvider(LocalActivityResultRegistryOwner provides refusing) {
                SpeechEndpointSettings(settings) {}
            }
        }
        val test = compose.onNodeWithText("Test connection")

        test.performClick()
        compose.waitUntil {
            compose
                .onAllNodesWithText("Speecher can't reach your network.", substring = true)
                .fetchSemanticsNodes()
                .isNotEmpty()
        }
        assertEquals(listOf(Manifest.permission.ACCESS_LOCAL_NETWORK), asked)

        // Android shows no prompt once refused, so the next test opens Speecher's settings.
        test.performClick()
        val activity = shadowOf(compose.activity)
        compose.waitUntil { activity.peekNextStartedActivity() != null }
        assertEquals(
            Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
            activity.nextStartedActivity.action,
        )
        assertEquals(1, asked.size)
    }
}
