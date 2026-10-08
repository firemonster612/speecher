package app.speecher.android.ui

import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.android.transfer.ComputerSettings
import app.speecher.android.transfer.withImported
import app.speecher.protocol.VocabularyWord
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class ComputerImportPageTest {
    @get:Rule val compose = createComposeRule()

    @Test
    fun `Done still takes the computer's larger use counts when nothing else differs`() {
        val phone =
            SpeecherSettings(vocabulary = listOf(VocabularyWord("Kubernetes", frequency = 1)))
        val computer =
            ComputerSettings(
                "pc",
                listOf(VocabularyWord("Kubernetes", frequency = 41, lastUsedMs = 200)),
                emptyList(),
                emptyMap(),
                emptyList(),
                emptyList(),
                "",
            )
        var saved = phone
        compose.setContent {
            ComputerImport(
                ImportState.Fetched(computer),
                phone,
                {},
                {},
                {},
                { saved = phone.withImported(it) },
            )
        }
        compose.onNodeWithText("Everything from pc is already on this phone.").assertExists()
        compose.onNodeWithText("Done").performClick()
        assertEquals(
            listOf(VocabularyWord("Kubernetes", frequency = 41, lastUsedMs = 200)),
            saved.vocabulary,
        )
    }
}
