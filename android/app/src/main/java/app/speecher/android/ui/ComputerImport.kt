package app.speecher.android.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.android.transfer.Changes
import app.speecher.android.transfer.ComputerSettings
import app.speecher.android.transfer.ImportFailure
import app.speecher.android.transfer.ImportPreview
import app.speecher.android.transfer.importPreview
import app.speecher.android.transfer.withImported

/** Where an import from the computer is. */
sealed interface ImportState {
    data class Fetching(val computer: String) : ImportState

    data class Failed(val failure: ImportFailure) : ImportState

    data class Fetched(val settings: ComputerSettings) : ImportState
}

/**
 * The Import from computer page: fetching, why it failed, or what importing would change against
 * the [phone]'s settings, with Cancel and Import.
 */
@Composable
fun ComputerImport(
    state: ImportState,
    phone: SpeecherSettings,
    onBack: () -> Unit,
    onScanAgain: () -> Unit,
    onImport: (ComputerSettings) -> Unit,
) {
    when (state) {
        is ImportState.Fetching ->
            StatusScreen(onBack) {
                CircularProgressIndicator()
                Text(
                    buildAnnotatedString {
                        append("Getting your settings from ")
                        bold(state.computer)
                        append("…")
                    },
                    textAlign = TextAlign.Center,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        is ImportState.Failed ->
            StatusScreen(onBack) {
                Text(
                    state.failure.title,
                    textAlign = TextAlign.Center,
                    color = MaterialTheme.colorScheme.error,
                    style = MaterialTheme.typography.headlineSmall,
                )
                if (state.failure.detail.isNotEmpty()) {
                    Text(
                        state.failure.detail,
                        textAlign = TextAlign.Center,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                Button(onScanAgain) { Text("Scan again") }
            }
        is ImportState.Fetched -> {
            val computer = state.settings
            val preview = importPreview(phone, computer)
            if (preview.changesNothing) {
                StatusScreen(onBack, bottomBar = { Buttons { Button(onBack) { Text("Done") } } }) {
                    Text(
                        "Everything from ${computer.computer} is already on this phone.",
                        textAlign = TextAlign.Center,
                    )
                }
            } else {
                SpeecherScreen(
                    TITLE,
                    onBack,
                    bottomBar = {
                        Buttons {
                            TextButton(onBack) { Text("Cancel") }
                            Button({ onImport(computer) }) { Text("Import") }
                        }
                    },
                ) {
                    ImportPreviewList(computer, phone, preview)
                }
            }
        }
    }
}

private const val TITLE = "Import from computer"

/** The page with [content] centred on it. */
@Composable
private fun StatusScreen(
    onBack: () -> Unit,
    bottomBar: @Composable () -> Unit = {},
    content: @Composable () -> Unit,
) {
    SpeecherScreen(
        TITLE,
        onBack,
        bottomBar = bottomBar,
        verticalArrangement = Arrangement.Center,
    ) {
        Column(
            Modifier.fillMaxWidth().padding(horizontal = 32.dp),
            Arrangement.spacedBy(16.dp),
            Alignment.CenterHorizontally,
        ) {
            content()
        }
    }
}

@Composable
private fun Buttons(content: @Composable () -> Unit) {
    Row(Modifier.fillMaxWidth().padding(16.dp), Arrangement.spacedBy(8.dp, Alignment.End)) {
        content()
    }
}

@Composable
private fun ImportPreviewList(
    computer: ComputerSettings,
    phone: SpeecherSettings,
    preview: ImportPreview,
) {
    Card(Modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, top = 16.dp)) {
        Text(
            buildAnnotatedString {
                append("From ")
                bold(computer.computer)
                append(
                    ". Nothing on this phone is deleted. Where both have the same item, the " +
                        "computer's version replaces the phone's."
                )
            },
            Modifier.padding(16.dp),
        )
    }
    val vocabulary = preview.vocabulary
    Group(
        "Vocabulary",
        vocabulary,
        count(computer.vocabulary.size, "term", "terms") to
            names(computer.vocabulary.map { it.term }),
        "Already on this phone" to
            "${gets(vocabulary.updated.size)} the computer's context or profiles",
    )
    val replacements = preview.replacements
    Group(
        "Replacements and snippets",
        replacements,
        count(computer.replacements.size, "replacement", "replacements") to
            names(computer.replacements.map { it.phrase }),
        "Already on this phone" to "${gets(replacements.updated.size)} the computer's text",
    )
    val labels = profileChoices(phone.withImported(computer))
    val profiles = preview.writingProfiles
    Group(
        "Writing Profiles",
        profiles,
        names(profiles.added.map(labels::getValue)) to
            "Your own profiles, with their tones and levels",
        names(profiles.updated.map(labels::getValue)) to "Different settings on the computer",
    )
    val tonesAndLevels = preview.tonesAndLevels
    Group(
        "Tones and cleanup levels",
        tonesAndLevels,
        names(tonesAndLevels.added) to "Your own tones and cleanup levels",
        names(tonesAndLevels.updated) to "Different names or instructions on the computer",
    )
    preview.additionalInstructions?.let {
        ChangeRow(
            "Additional instructions",
            "\"$it\"",
            "Replaces",
            MaterialTheme.colorScheme.tertiaryContainer,
        )
    }
}

/**
 * One kind of item under [title], left out when the import changes none: a row about those added,
 * and one about those updated, each as headline and supporting text.
 */
@Composable
private fun Group(
    title: String,
    changes: Changes<*>,
    addedRow: Pair<String, String>,
    updatedRow: Pair<String, String>,
) {
    if (changes.isEmpty) return
    Section(title)
    val colors = MaterialTheme.colorScheme
    if (changes.added.isNotEmpty()) {
        ChangeRow(
            addedRow.first,
            addedRow.second,
            "${changes.added.size} new",
            colors.secondaryContainer,
        )
    }
    if (changes.updated.isNotEmpty()) {
        ChangeRow(
            updatedRow.first,
            updatedRow.second,
            "${changes.updated.size} updated",
            colors.tertiaryContainer,
        )
    }
}

/** A row of the preview, with a tag saying what the import does. */
@Composable
private fun ChangeRow(headline: String, supporting: String, tag: String, tagColor: Color) {
    ListItem(
        headlineContent = { Text(headline) },
        supportingContent = { Text(supporting, maxLines = 2, overflow = TextOverflow.Ellipsis) },
        trailingContent = { Tag(tag, tagColor) },
        colors = rowColors(),
    )
}

private fun gets(n: Int) = if (n == 1) "1 gets" else "$n get"

@Composable
private fun Tag(text: String, color: Color) {
    Surface(shape = MaterialTheme.shapes.small, color = color) {
        Text(
            text,
            Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
            style = MaterialTheme.typography.labelMedium,
        )
    }
}

/** The first few [names], then how many more there are. */
private fun names(names: List<String>): String =
    if (names.size <= SHOWN_NAMES) names.joinToString(", ")
    else names.take(SHOWN_NAMES).joinToString(", ") + " and ${names.size - SHOWN_NAMES} more"

private const val SHOWN_NAMES = 4

private fun AnnotatedString.Builder.bold(text: String) {
    withStyle(SpanStyle(fontWeight = FontWeight.Bold)) { append(text) }
}
