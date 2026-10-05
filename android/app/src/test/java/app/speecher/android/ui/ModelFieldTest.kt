package app.speecher.android.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performTextClearance
import androidx.compose.ui.test.performTextInput
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SpeecherSettings
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class ModelFieldTest {
    @get:Rule val compose = createComposeRule()

    @Test
    fun `a typed model id is saved, a cleared one is not, and Haiku shows the caution`() {
        var settings by mutableStateOf(SpeecherSettings(refinementProvider = Provider.Claude))
        compose.setContent {
            Column {
                SettingsPageContent(
                    SettingsPage.Refinement,
                    settings,
                    setOf(Provider.Claude),
                    { settings = it },
                    {},
                    {},
                )
            }
        }
        val field = compose.onAllNodes(hasSetTextAction()).onFirst()
        field.performTextClearance()
        assertEquals("claude-sonnet-5-5", settings.claudeRefinement.model)
        field.performTextInput("claude-haiku-4-5")
        assertEquals("claude-haiku-4-5", settings.claudeRefinement.model)
        compose.onNodeWithText("Haiku may treat transcript as instructions.").assertExists()
    }
}
