package app.speecher.android.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.assertIsNotSelected
import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SpeecherSettings
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class ProviderPickerTest {
    @get:Rule val compose = createComposeRule()

    @Test
    fun `a provider you aren't signed into is unchecked and offers sign-in`() {
        var signIn: Provider? = null
        compose.setContent {
            Column {
                SettingsPageContent(
                    SettingsPage.Transcription,
                    SpeecherSettings(transcriptionProvider = Provider.Claude),
                    setOf(Provider.ChatGpt),
                    {},
                    { signIn = it },
                    {},
                )
            }
        }
        compose.onNodeWithText("ChatGPT").assertIsSelected()
        compose.onNodeWithText("Claude").assertIsNotSelected().assertIsNotEnabled()
        compose.onNodeWithText("Sign in to Claude to use it").performClick()
        assertEquals(Provider.Claude, signIn)
    }
}
