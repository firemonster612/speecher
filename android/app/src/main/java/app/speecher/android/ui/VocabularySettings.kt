package app.speecher.android.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Checkbox
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.PlainTooltip
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TooltipAnchorPosition
import androidx.compose.material3.TooltipBox
import androidx.compose.material3.TooltipDefaults
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.material3.rememberTooltipState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.R
import app.speecher.android.provider.Provider
import app.speecher.android.provider.SpeechProvider
import app.speecher.android.settings.SpeecherSettings
import app.speecher.protocol.MAX_REFINEMENT_TERMS
import app.speecher.protocol.MAX_SPEECH_TERMS
import app.speecher.protocol.VocabularyWord
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import app.speecher.protocol.claudeVoiceKeyterms
import app.speecher.protocol.normalizedVocabulary
import app.speecher.protocol.parseVocabularyCsv
import app.speecher.protocol.sameTerm
import app.speecher.protocol.speechTerms
import java.io.IOException
import java.text.DateFormat
import java.util.Date
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

// The Vocabulary page: the word list, the word editor, CSV import, what the speech service gets,
// and its previews.

@Composable
internal fun VocabularySettings(settings: SpeecherSettings, onChange: (SpeecherSettings) -> Unit) {
    val takesKeyTerms = settings.transcriptionProvider != SpeechProvider.ChatGpt
    Text(
        "Refinement uses the terms for the dictation's Writing Profile. " +
            when (settings.transcriptionProvider) {
                SpeechProvider.Claude -> "Key terms also go to Claude Voice."
                SpeechProvider.Endpoint ->
                    "Key terms also go to the Custom Endpoint, as its prompt."
                SpeechProvider.ChatGpt -> "This speech service takes no key terms."
            },
        Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
        style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
    // The settings as they are when an import finishes reading, which may be after a dictation
    // has counted uses.
    val latest by rememberUpdatedState(settings)
    // Stored in the desktop's order, the one the speech hints are cut from.
    fun change(words: List<VocabularyWord>) =
        onChange(latest.copy(vocabulary = normalizedVocabulary(words)))
    val keyTerms = keyTerms(settings)
    val profiles = profileChoices(settings)
    // The term open in the editor, empty for a new one. Held by term, as a dictation can update
    // the word's use count while the sheet is open.
    var editing by rememberSaveable { mutableStateOf<String?>(null) }
    if (settings.vocabulary.isEmpty())
        ListItem(
            headlineContent = { Text("No vocabulary terms") },
            supportingContent = {
                Text("Add names and words the speech service should spell your way.")
            },
            colors = rowColors(),
        )
    settings.vocabulary.forEach { word ->
        ListItem(
            headlineContent = { Text(word.term) },
            supportingContent = {
                Column {
                    if (word.context.isNotEmpty())
                        Text(word.context, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    if (word.profiles.isEmpty()) Text("All profiles")
                    else
                        Text(
                            profiles.filterKeys { it in word.profiles }.values.joinToString(", "),
                            color = MaterialTheme.colorScheme.onSurface,
                        )
                    Text(usageLine(word))
                }
            },
            // The desktop's Key term column: a microphone when the speech service gets the word,
            // faint for a key term it does not. ListItem tops the leading and trailing slots of a
            // three-line row, so they fill its height.
            leadingContent = {
                Box(Modifier.fillMaxHeight(), contentAlignment = Alignment.Center) {
                    // Without case, as the desktop matches.
                    val sent = keyTerms.any { sameTerm(it, word.term) }
                    IconSlot(
                        R.drawable.ic_mic.takeIf { word.keyTerm },
                        when {
                            !word.keyTerm -> "Not a key term: refinement only."
                            sent -> "Key term: sent to the speech service as a hint."
                            takesKeyTerms ->
                                "Key term, but the speech service does not take it, so it is not sent."
                            else -> "Key term, but this speech service takes none."
                        },
                        faint = !sent,
                    )
                }
            },
            trailingContent = {
                Box(Modifier.fillMaxHeight(), contentAlignment = Alignment.Center) {
                    IconButton({ change(settings.vocabulary - word) }) {
                        Icon(
                            painterResource(R.drawable.ic_close),
                            contentDescription = "Remove ${word.term}",
                        )
                    }
                }
            },
            modifier = Modifier.height(IntrinsicSize.Min).clickable { editing = word.term },
            colors = rowColors(),
        )
    }
    var importError by rememberSaveable { mutableStateOf<String?>(null) }
    val resolver = LocalContext.current.contentResolver
    val scope = rememberCoroutineScope()
    val import =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri == null) return@rememberLauncherForActivityResult
            scope.launch {
                importError =
                    try {
                        // A cloud provider can take its time, so not on the main thread.
                        val words =
                            withContext(Dispatchers.IO) {
                                val csv =
                                    resolver.openInputStream(uri)?.use { it.readBytes() }
                                        ?: throw IOException("no stream for $uri")
                                parseVocabularyCsv(csv.decodeToString())
                            }
                        // After the list, so a term already in it keeps its own settings.
                        change(latest.vocabulary + words)
                        null
                    } catch (_: IOException) {
                        "Could not read the file."
                    } catch (e: IllegalArgumentException) {
                        e.message
                    }
            }
        }
    Row(Modifier.padding(16.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        FilledTonalButton({ editing = "" }) { Text("Add") }
        // Any type, as a CSV file often comes labelled as something else; the parser decides.
        OutlinedButton({
            importError = null
            import.launch(arrayOf("*/*"))
        }) {
            Text("Import CSV…")
        }
    }
    importError?.let {
        Column(Modifier.padding(start = 16.dp, end = 16.dp, bottom = 8.dp)) {
            Text(
                "Vocabulary not imported",
                style = MaterialTheme.typography.titleSmall,
                color = MaterialTheme.colorScheme.error,
            )
            Text(it, style = MaterialTheme.typography.bodyMedium)
        }
    }
    ListItem(
        headlineContent = { Text("Limit") },
        supportingContent = { Text(vocabularyLimit(settings)) },
        colors = rowColors(),
    )
    editing?.let { term ->
        WordEditor(
            settings.vocabulary.firstOrNull { it.term == term } ?: VocabularyWord(""),
            profiles,
            settings.vocabulary.map { it.term }.filter { it != term },
            onDismiss = { editing = null },
        ) { next ->
            change(settings.vocabulary.filter { it.term != term } + next)
            editing = null
        }
    }
}

/** The desktop's Source, Uses and Last used columns, on one line. */
private fun usageLine(word: VocabularyWord): String {
    val source =
        when (word.source) {
            "csv" -> "Imported"
            "learned" -> "Learned"
            else -> "Added"
        }
    val lastUsed =
        if (word.lastUsedMs > 0)
            "Last used " +
                DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)
                    .format(Date(word.lastUsedMs))
        else "Never used"
    // A narrow row wraps at a separator rather than inside the date.
    return "$source · ${count(word.frequency, "use", "uses")} · ${lastUsed.replace(' ', '\u00A0')}"
}

/**
 * One of a vocabulary row's leading icon slots, a fixed width so the rows line up: the icon in the
 * text colour, or the disabled colour when [faint], with [description] as its tooltip. Without an
 * icon the slot is empty and a screen reader still reads the description.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun IconSlot(icon: Int?, description: String, faint: Boolean = false) {
    Box(Modifier.size(24.dp)) {
        if (icon == null) {
            Box(Modifier.matchParentSize().semantics { contentDescription = description })
            return@Box
        }
        TooltipBox(
            TooltipDefaults.rememberTooltipPositionProvider(TooltipAnchorPosition.Above),
            tooltip = { PlainTooltip { Text(description) } },
            state = rememberTooltipState(),
        ) {
            val colors = rowColors()
            Icon(
                painterResource(icon),
                contentDescription = description,
                tint = if (faint) colors.disabledLeadingIconColor else colors.headlineColor,
            )
        }
    }
}

/**
 * A word's term, its context and the profiles it is limited to, in a sheet. Saved once the term is
 * set and, unless it is the word's own term, none of [taken], ignoring case, and a limit names at
 * least one profile. Its own term passes so a list that earlier releases let hold two spellings of
 * a word can still be edited.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun WordEditor(
    word: VocabularyWord,
    profiles: Map<WritingProfile, String>,
    taken: List<String>,
    onDismiss: () -> Unit,
    onSave: (VocabularyWord) -> Unit,
) {
    var term by rememberSaveable { mutableStateOf(word.term) }
    var context by rememberSaveable { mutableStateOf(word.context) }
    var keyTerm by rememberSaveable { mutableStateOf(word.keyTerm) }
    var limited by rememberSaveable { mutableStateOf(word.profiles.isNotEmpty()) }
    var chosen by remember { mutableStateOf(word.profiles) }
    val duplicate = term.trim() != word.term && taken.any { sameTerm(it, term.trim()) }
    ModalBottomSheet(onDismiss, sheetState = rememberModalBottomSheetState(true)) {
        Column(
            Modifier.verticalScroll(rememberScrollState()).padding(bottom = 16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(
                word.term.ifEmpty { "New term" },
                Modifier.padding(horizontal = 16.dp),
                style = MaterialTheme.typography.headlineSmall,
            )
            OutlinedTextField(
                term,
                { term = it },
                Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                label = { Text("Term") },
                singleLine = true,
                isError = duplicate,
                supportingText =
                    if (duplicate) ({ Text("That word is already listed.") }) else null,
            )
            ChoiceRow(
                "Key term",
                Role.Checkbox,
                keyTerm,
                supporting =
                    "Sent to the speech service as a hint, so it hears the term. Refinement uses " +
                        "every term either way.",
            ) {
                keyTerm = !keyTerm
            }
            OutlinedTextField(
                context,
                { context = it },
                Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                label = { Text("Context") },
                placeholder = {
                    Text(
                        "What it means and when it applies, such as \"the container platform, " +
                            "when I talk about clusters or deploys\"."
                    )
                },
                supportingText = {
                    Text("Refinement reads this to decide when the words you said mean this term.")
                },
                minLines = 2,
                maxLines = 6,
            )
            Column(Modifier.selectableGroup()) {
                Text(
                    "Profiles",
                    Modifier.padding(horizontal = 16.dp),
                    style = MaterialTheme.typography.titleSmall,
                )
                ChoiceRow("Every Writing Profile", Role.RadioButton, !limited) { limited = false }
                ChoiceRow("Only these Writing Profiles:", Role.RadioButton, limited) {
                    limited = true
                }
            }
            Column(Modifier.padding(start = 40.dp)) {
                profiles.forEach { (profile, label) ->
                    ChoiceRow(label, Role.Checkbox, profile in chosen, enabled = limited) {
                        chosen = if (profile in chosen) chosen - profile else chosen + profile
                    }
                }
            }
            Text(
                "Under any other profile, neither refinement nor the speech service gets this term.",
                Modifier.padding(horizontal = 16.dp),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            SheetActions(
                term.isNotBlank() && !duplicate && (!limited || chosen.isNotEmpty()),
                onDismiss,
                "Save",
            ) {
                // Priority, no longer edited, and the usage counts stay as they were.
                onSave(
                    word.copy(
                        term = term.trim(),
                        context = context.trim(),
                        profiles = if (limited) chosen else emptySet(),
                        keyTerm = keyTerm,
                    )
                )
            }
        }
    }
}

/**
 * A radio button or checkbox and its label, with [supporting] text under it if given, the whole row
 * the target.
 */
@Composable
private fun ChoiceRow(
    label: String,
    role: Role,
    selected: Boolean,
    enabled: Boolean = true,
    supporting: String? = null,
    onClick: () -> Unit,
) {
    val target =
        if (role == Role.RadioButton)
            Modifier.selectable(selected, enabled = enabled, role = role, onClick = onClick)
        else Modifier.toggleable(selected, enabled = enabled, role = role) { onClick() }
    Row(
        Modifier.fillMaxWidth().then(target).padding(horizontal = 16.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (role == Role.RadioButton) RadioButton(selected, null, enabled = enabled)
        else Checkbox(selected, null, enabled = enabled)
        // Material's disabled content opacity, as the control beside it uses.
        val alpha = if (enabled) 1f else 0.38f
        Column(Modifier.padding(start = 16.dp)) {
            Text(
                label,
                style = MaterialTheme.typography.bodyLarge,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = alpha),
            )
            supporting?.let {
                Text(
                    it,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = alpha),
                )
            }
        }
    }
}

/**
 * The words the speech service receives as key terms: those that fit both speech limits, then, for
 * Claude Voice, its header. A Custom Endpoint takes them all as its prompt; ChatGPT takes none.
 */
internal fun keyTerms(settings: SpeecherSettings): Set<String> {
    val terms = speechTerms(settings.vocabulary)
    return when (settings.transcriptionProvider) {
        SpeechProvider.ChatGpt -> emptySet()
        SpeechProvider.Claude -> claudeVoiceKeyterms(terms).toSet()
        SpeechProvider.Endpoint -> terms.toSet()
    }
}

/**
 * What the list amounts to: how many terms, how many the speech service gets, and how many
 * refinement gets. The desktop's Library.keyTermsText.
 */
internal fun vocabularyLimit(settings: SpeecherSettings): String {
    val terms = settings.vocabulary.size
    val refinement =
        if (terms > MAX_REFINEMENT_TERMS) "the first $MAX_REFINEMENT_TERMS are used for refinement"
        else "all are used for refinement"
    if (settings.transcriptionProvider == SpeechProvider.ChatGpt)
        return if (terms == 1) "1 term, used for refinement" else "$terms terms, $refinement"
    val sent = keyTerms(settings).size
    if (sent < terms) {
        val keyTerms = if (sent == 1) "1 is a key term" else "$sent are key terms"
        return "${count(terms, "term", "terms")}. $keyTerms, and $refinement."
    }
    return "$terms of $MAX_SPEECH_TERMS key terms"
}

@Composable
private fun VocabularyPreview(vocabulary: List<VocabularyWord>) = SpeecherTheme {
    var settings by remember {
        mutableStateOf(
            SpeecherSettings(
                transcriptionProvider = SpeechProvider.Claude,
                writingProfiles =
                    SpeecherSettings().writingProfiles +
                        (previewStandup to WritingProfileSettings(name = "Standup notes")),
                vocabulary = normalizedVocabulary(vocabulary),
            )
        )
    }
    SpeecherScreen(SettingsPage.Vocabulary.title, onBack = {}) {
        SettingsPageContent(
            SettingsPage.Vocabulary,
            settings,
            Provider.entries.toSet(),
            { settings = it },
            {},
            {},
            chipEnabled = true,
            onTurnOnChip = {},
        )
    }
}

private val previewStandup = WritingProfile("custom_standup")

@PreviewLightDark
@Composable
internal fun SettingsVocabularyPreview() =
    VocabularyPreview(
        listOf(
            VocabularyWord(
                "Kubernetes",
                "The container platform, when I talk about clusters, pods or deploys.",
                setOf(WritingProfile.Work, WritingProfile.AiCoding),
                priority = true,
                frequency = 14,
                lastUsedMs = 1_791_020_460_000,
            ),
            VocabularyWord("Speecher", priority = true),
            VocabularyWord(
                "Lúcia",
                "My sister.",
                setOf(WritingProfile.Personal),
                source = "csv",
                frequency = 3,
                lastUsedMs = 1_790_500_000_000,
            ),
            VocabularyWord(
                "Grafana",
                "",
                setOf(WritingProfile.Work, previewStandup),
                keyTerm = false,
                source = "csv",
            ),
        )
    )

@PreviewLightDark
@Composable
internal fun SettingsVocabularyEmptyPreview() = VocabularyPreview(emptyList())
