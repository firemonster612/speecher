package app.speecher.android.update

import android.content.Context
import androidx.core.content.edit
import app.speecher.android.BuildConfig
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

@RunWith(RobolectricTestRunner::class)
class UpdateViewModelTest {
    // Saved the way earlier releases did, from before the channel setting.
    @Test
    fun `shows the offer an earlier release saved`() {
        val app = RuntimeEnvironment.getApplication()
        app.getSharedPreferences("updates", Context.MODE_PRIVATE).edit {
            putString("installed-version", BuildConfig.VERSION_NAME)
            putString("version", "9.9.9")
            putString("url", "https://example.com/Speecher-9.9.9.apk")
            putString("page", "https://example.com/android-v9.9.9")
        }
        val updates = UpdateViewModel(app)

        updates.showSavedOffer(UpdateChannel.Stable)

        assertEquals(
            UpdateState.Available(
                ApkUpdate(
                    "9.9.9",
                    "https://example.com/Speecher-9.9.9.apk",
                    "https://example.com/android-v9.9.9",
                )
            ),
            updates.state,
        )
    }
}
