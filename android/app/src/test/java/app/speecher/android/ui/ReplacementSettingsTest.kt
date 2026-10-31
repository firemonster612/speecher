package app.speecher.android.ui

import android.net.Uri
import android.os.Looper
import androidx.activity.ComponentActivity
import androidx.activity.compose.LocalActivityResultRegistryOwner
import androidx.activity.result.ActivityResultRegistry
import androidx.activity.result.ActivityResultRegistryOwner
import androidx.activity.result.contract.ActivityResultContract
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.core.app.ActivityOptionsCompat
import app.speecher.android.settings.SpeecherSettings
import app.speecher.protocol.Replacement
import java.io.ByteArrayInputStream
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
class ReplacementSettingsTest {
    @get:Rule val compose = createAndroidComposeRule<ComponentActivity>()

    @Test
    fun `importing snippets reads the file off the main thread`() {
        val file = Uri.parse("content://documents/snippets.json")
        val readOnMain = mutableListOf<Boolean>()
        val json = """{"sig":"Best, Sam"}""".toByteArray()
        shadowOf(compose.activity.contentResolver)
            .registerInputStream(
                file,
                object : ByteArrayInputStream(json) {
                    override fun read(b: ByteArray, off: Int, len: Int): Int {
                        readOnMain.add(Looper.myLooper() == Looper.getMainLooper())
                        return super.read(b, off, len)
                    }
                },
            )
        // The document picker, choosing the file.
        val picker =
            object : ActivityResultRegistryOwner {
                override val activityResultRegistry =
                    object : ActivityResultRegistry() {
                        override fun <I, O> onLaunch(
                            requestCode: Int,
                            contract: ActivityResultContract<I, O>,
                            input: I,
                            options: ActivityOptionsCompat?,
                        ) {
                            dispatchResult(requestCode, file)
                        }
                    }
            }
        var settings by mutableStateOf(SpeecherSettings())
        compose.setContent {
            CompositionLocalProvider(LocalActivityResultRegistryOwner provides picker) {
                ReplacementSettings(settings) { settings = it }
            }
        }

        compose.onNodeWithText("Import snippets JSON…").performClick()
        compose.waitUntil { settings.replacements.isNotEmpty() }
        assertEquals(listOf(Replacement("sig", "Best, Sam")), settings.replacements)
        assertEquals(setOf(false), readOnMain.toSet())
    }
}
