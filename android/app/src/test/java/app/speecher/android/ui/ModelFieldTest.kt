package app.speecher.android.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.test.assertTextContains
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.onLast
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performTextReplacement
import androidx.compose.ui.test.requestFocus
import app.speecher.android.dictation.CleanupProvider
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.RefinementChoice
import app.speecher.android.dictation.SpeecherSettings
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class ModelFieldTest {
    @get:Rule val compose = createComposeRule()

    private var settings by
        mutableStateOf(
            SpeecherSettings(
                refinementProvider = CleanupProvider.Claude,
                claudeRefinement = RefinementChoice("claude-haiku-4-5", "low"),
            )
        )

    private fun show() = compose.setContent {
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

    private val field
        get() = compose.onAllNodes(hasSetTextAction()).onFirst()

    @Test
    fun `deleting a model letter by letter saves the default, shown by name once left`() {
        show()
        field.assertTextContains("Claude Haiku 4.5")
        compose.onNodeWithText("Haiku may treat transcript as instructions.").assertExists()
        for (length in "Claude Haiku 4.5".length - 1 downTo 0) {
            field.performTextReplacement("Claude Haiku 4.5".take(length))
        }
        assertEquals("claude-sonnet-5-5", settings.claudeRefinement.model)
        compose.onNodeWithText("Haiku may treat transcript as instructions.").assertDoesNotExist()
        // Leaving for the instructions field.
        compose.onAllNodes(hasSetTextAction()).onLast().requestFocus()
        field.assertTextContains("Claude Sonnet 5.5")
    }

    @Test
    fun `a typed name saves its id and an unknown id saves as typed`() {
        settings =
            settings.withRefinement(Provider.Claude, RefinementChoice("claude-opus-5", "low"))
        show()
        field.performTextReplacement("Claude Opus 5.5")
        assertEquals("claude-opus-5-5", settings.claudeRefinement.model)
        field.performTextReplacement("claude-opus-9 ")
        assertEquals("claude-opus-9", settings.claudeRefinement.model)
        compose.onAllNodes(hasSetTextAction()).onLast().requestFocus()
        field.assertTextContains("claude-opus-9")
    }
}
