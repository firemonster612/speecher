package app.speecher.android.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.R
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.protocol.Replacement
import app.speecher.protocol.importReplacements
import app.speecher.protocol.normalizedPhrase
import app.speecher.protocol.replacementProblems
import java.io.IOException

/**
 * The desktop's Replacements & snippets page: each spoken phrase and its text, editable in a sheet,
 * and snippets imported from a JSON file, after the ones already listed.
 */
@Composable
internal fun ReplacementSettings(settings: SpeecherSettings, onChange: (SpeecherSettings) -> Unit) {
    val context = LocalContext.current
    var failure by rememberSaveable { mutableStateOf<String?>(null) }
    // The rule open in the editor; one not in the list yet is being added.
    var editing by remember { mutableStateOf<Replacement?>(null) }
    val import =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri == null) return@rememberLauncherForActivityResult
            failure =
                try {
                    val json =
                        context.contentResolver.openInputStream(uri)?.use {
                            it.readBytes().decodeToString()
                        } ?: throw IOException()
                    onChange(
                        settings.copy(
                            replacements = importReplacements(settings.replacements, json)
                        )
                    )
                    null
                } catch (e: IllegalArgumentException) {
                    e.message
                } catch (_: IOException) {
                    "The file could not be read."
                }
        }
    Text(
        "Replace a spoken phrase with exact text, including multi-line snippets. {date} and " +
            "{time} in the text become today's date and the current time. Matching ignores case " +
            "and treats punctuation as spaces.",
        Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
        style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
    if (settings.replacements.isEmpty()) {
        ListItem(
            headlineContent = { Text("No replacements or snippets") },
            supportingContent = {
                Text("Add a spoken phrase and the exact text to put in its place.")
            },
            colors = rowColors(),
        )
    }
    settings.replacements.forEach { rule ->
        ListItem(
            headlineContent = { Text(rule.phrase) },
            supportingContent = { Text(rule.text, maxLines = 2, overflow = TextOverflow.Ellipsis) },
            trailingContent = {
                IconButton({
                    onChange(settings.copy(replacements = settings.replacements - rule))
                }) {
                    Icon(
                        painterResource(R.drawable.ic_close),
                        contentDescription = "Remove ${rule.phrase}",
                    )
                }
            },
            modifier = Modifier.clickable { editing = rule },
            colors = rowColors(),
        )
    }
    Row(Modifier.padding(16.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        FilledTonalButton({ editing = Replacement("", "") }) { Text("Add replacement") }
        // Any file, as the desktop offers All files: a bad one is reported as invalid JSON.
        OutlinedButton({ import.launch(arrayOf("*/*")) }) { Text("Import snippets JSON…") }
    }
    failure?.let { ImportFailure(it) }
    editing?.let { rule ->
        ReplacementEditor(
            rule,
            settings.replacements.filter { it != rule },
            onDismiss = { editing = null },
        ) { next ->
            val replacements =
                if (rule in settings.replacements)
                    settings.replacements.map { if (it == rule) next else it }
                else settings.replacements + next
            onChange(settings.copy(replacements = replacements))
            editing = null
        }
    }
}

/** Why the chosen file added nothing, under the page's buttons. */
@Composable
private fun ImportFailure(message: String) {
    Column(Modifier.padding(horizontal = 16.dp)) {
        Text(
            "Snippets not imported",
            style = MaterialTheme.typography.titleSmall,
            color = MaterialTheme.colorScheme.error,
        )
        Text(
            message,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.error,
        )
    }
}

/**
 * A rule's spoken phrase and its text, in a sheet. Saved once it could join the [others] without a
 * problem: the phrase has a letter or number and reads unlike theirs, and the text is set.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ReplacementEditor(
    rule: Replacement,
    others: List<Replacement>,
    onDismiss: () -> Unit,
    onSave: (Replacement) -> Unit,
) {
    var phrase by rememberSaveable { mutableStateOf(rule.phrase) }
    var text by rememberSaveable { mutableStateOf(rule.text) }
    val normalized = normalizedPhrase(phrase)
    val duplicate =
        normalized.isNotEmpty() && others.any { normalizedPhrase(it.phrase) == normalized }
    ModalBottomSheet(onDismiss, sheetState = rememberModalBottomSheetState(true)) {
        Column(
            Modifier.verticalScroll(rememberScrollState()).padding(bottom = 16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(
                rule.phrase.ifEmpty { "New replacement" },
                Modifier.padding(horizontal = 16.dp),
                style = MaterialTheme.typography.headlineSmall,
            )
            OutlinedTextField(
                phrase,
                { phrase = it },
                Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                label = { Text("Spoken phrase") },
                singleLine = true,
                isError = duplicate,
                supportingText =
                    if (duplicate) ({ Text("That phrase is already listed.") }) else null,
            )
            OutlinedTextField(
                text,
                { text = it },
                Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                label = { Text("Exact replacement or snippet") },
                placeholder = { Text("Sent on {date} at {time}") },
                minLines = 3,
                maxLines = 8,
            )
            SheetActions(
                replacementProblems(others + Replacement(phrase, text)).isEmpty(),
                onDismiss,
                "Save",
            ) {
                onSave(Replacement(phrase.trim(), text))
            }
        }
    }
}

@PreviewLightDark
@Composable
internal fun SettingsReplacementsPreview() = SpeecherTheme {
    var settings by remember {
        mutableStateOf(
            SpeecherSettings(
                replacements =
                    listOf(
                        Replacement("my email", "efox@example.com"),
                        Replacement("sign off", "Thanks,\nEnzo Fox\nSent on {date} at {time}"),
                        Replacement("home address", "123 Main Street\nSpringfield"),
                    )
            )
        )
    }
    SpeecherScreen(SettingsPage.Replacements.title, onBack = {}) {
        ReplacementSettings(settings, { settings = it })
    }
}

@PreviewLightDark
@Composable
internal fun SettingsReplacementsEmptyPreview() = SpeecherTheme {
    SpeecherScreen(SettingsPage.Replacements.title, onBack = {}) {
        ReplacementSettings(SpeecherSettings(), {})
        ImportFailure("Row 2 duplicates the normalized spoken phrase from row 1.")
    }
}
