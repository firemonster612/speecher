package app.speecher.android.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import app.speecher.android.R
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.android.dictation.profileDeletionNotice
import app.speecher.protocol.AppCategory
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.CustomCleanupLevel
import app.speecher.protocol.CustomTone
import app.speecher.protocol.RecognitionRule
import app.speecher.protocol.Tone
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import app.speecher.protocol.builtInDictationSystemPrompt
import app.speecher.protocol.builtInRules
import app.speecher.protocol.cleanupLevelId
import app.speecher.protocol.customChoiceId
import app.speecher.protocol.toneId
import app.speecher.protocol.withCleanupLevel
import app.speecher.protocol.withTone

// Labels from the desktop's Target.cpp, in its order.
private val profileLabels =
    mapOf(
        WritingProfile.Work to "Work",
        WritingProfile.Email to "Email",
        WritingProfile.Personal to "Personal",
        WritingProfile.AiCoding to "AI coding",
        WritingProfile.Other to "Other",
    )

// The desktop's appCategoryLabel, in the order its rules offer them.
private val appTypeLabels =
    mapOf(
        AppCategory.General to "Other app",
        AppCategory.Terminal to "Terminal",
        AppCategory.Browser to "Browser",
        AppCategory.Email to "Email",
        AppCategory.Office to "Office",
        AppCategory.CodeEditor to "Code editor",
        AppCategory.AiCoding to "AI coding",
    )

private val cleanupLabels =
    mapOf(
        CleanupStrength.None to "None",
        CleanupStrength.LightCleanup to "Light",
        CleanupStrength.Balanced to "Medium",
        CleanupStrength.StrongPolish to "High",
    )

/** What a custom cleanup level can build on. */
private val baseLabels =
    cleanupLabels - CleanupStrength.None + (CleanupStrength.CustomOnly to "Custom only")

private val toneLabels =
    mapOf(
        Tone.None to "No tone override",
        Tone.Formal to "Formal",
        Tone.Casual to "Casual",
        Tone.VeryCasual to "Very casual",
        Tone.Excited to "Excited",
        Tone.GenZ to "Gen Z",
    )

/** Every level a profile can choose, by id: the built-ins, then the custom ones. */
internal fun cleanupChoices(settings: SpeecherSettings): Map<String, String> =
    cleanupLabels.mapKeys { it.key.id } +
        settings.customCleanupLevels.associate { it.id to it.name }

/** Every profile, by id: the built-ins, then the custom ones by name. */
internal fun profileChoices(settings: SpeecherSettings): Map<WritingProfile, String> =
    profileLabels +
        settings.writingProfiles.filterKeys { !it.isBuiltIn }.mapValues { it.value.name }

/** Every tone a profile can choose, by id: no override and the built-ins, then the custom ones. */
internal fun toneChoices(settings: SpeecherSettings): Map<String, String> =
    toneLabels.mapKeys { it.key.id } + settings.customTones.associate { it.id to it.name }

@Composable
internal fun ProfileSettings(settings: SpeecherSettings, onChange: (SpeecherSettings) -> Unit) {
    PageIntro("Choose a cleanup level, a tone and optional instructions for each profile.")
    val profiles = profileChoices(settings)
    profiles.forEach { (profile, label) ->
        val behavior = settings.writingProfiles.getValue(profile)
        fun update(next: WritingProfileSettings) =
            onChange(settings.copy(writingProfiles = settings.writingProfiles + (profile to next)))
        Section(label)
        if (!profile.isBuiltIn) {
            OutlinedTextField(
                behavior.name,
                { update(behavior.copy(name = it)) },
                Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                label = { Text("Name") },
                singleLine = true,
                isError = behavior.name.isBlank(),
            )
        }
        DropdownRow("Cleanup", cleanupChoices(settings), behavior.cleanupLevelId) {
            update(behavior.withCleanupLevel(it))
        }
        DropdownRow("Tone", toneChoices(settings), behavior.toneId) {
            update(behavior.withTone(it))
        }
        OutlinedTextField(
            behavior.instructions,
            { update(behavior.copy(instructions = it)) },
            Modifier.fillMaxWidth().padding(horizontal = 16.dp),
            label = { Text("Instructions") },
            minLines = 2,
        )
        if (!profile.isBuiltIn) {
            DeleteProfile(settings.profileDeletionNotice(profile)) {
                onChange(settings.withWritingProfiles(settings.writingProfiles - profile))
            }
        }
    }
    AddChoice("Add profile", "Instructions", profiles.values) { name, instructions ->
        val id = customChoiceId(name, settings.writingProfiles.keys.map { it.id })
        onChange(
            settings.copy(
                writingProfiles =
                    settings.writingProfiles +
                        (WritingProfile(id) to
                            WritingProfileSettings(instructions = instructions, name = name))
            )
        )
    }
}

@Composable
internal fun AppRuleSettings(settings: SpeecherSettings, onChange: (SpeecherSettings) -> Unit) {
    PageIntro(
        "Built-in rules are read-only. Your rules come first and can set the app type, the " +
            "writing profile, or both."
    )
    val profiles = profileChoices(settings)
    BuiltInRules(profiles)
    val appTypes = mapOf<AppCategory?, String>(null to "Automatic") + appTypeLabels
    val ruleProfiles = mapOf<WritingProfile?, String>(null to "Automatic") + profiles
    settings.appRules.forEachIndexed { index, rule ->
        RuleEditor(
            rule,
            appTypes,
            ruleProfiles,
            onEdit = { next ->
                onChange(
                    settings.copy(
                        appRules =
                            settings.appRules.mapIndexed { at, it -> if (at == index) next else it }
                    )
                )
            },
            onDelete = {
                onChange(
                    settings.copy(
                        appRules = settings.appRules.filterIndexed { at, _ -> at != index }
                    )
                )
            },
        )
    }
    AddRule(appTypes, ruleProfiles) { onChange(settings.copy(appRules = settings.appRules + it)) }
}

@Composable
internal fun ToneSettings(settings: SpeecherSettings, onChange: (SpeecherSettings) -> Unit) {
    PageIntro("Built-in: ${toneLabels.values.drop(1).joinToString(", ")}")
    settings.customTones.forEach { tone ->
        CustomChoice(
            tone.name,
            tone.instruction,
            "Instruction",
            onEdit = { name, text ->
                onChange(
                    settings.withCustomChoices(
                        tones =
                            settings.customTones.map {
                                if (it.id == tone.id) it.copy(name = name, instruction = text)
                                else it
                            }
                    )
                )
            },
            onDelete = {
                onChange(settings.withCustomChoices(tones = settings.customTones - tone))
            },
        )
    }
    AddChoice("Add tone", "Instruction", toneChoices(settings).values, needsText = true) {
        name,
        instruction ->
        val id = customChoiceId(name, settings.customTones.map { it.id })
        onChange(
            settings.withCustomChoices(
                tones = settings.customTones + CustomTone(id, name, instruction)
            )
        )
    }
}

@Composable
internal fun CleanupLevelSettings(
    settings: SpeecherSettings,
    onChange: (SpeecherSettings) -> Unit,
) {
    PageIntro("Built-in: ${cleanupLabels.values.drop(1).joinToString(", ")}")
    settings.customCleanupLevels.forEach { level ->
        fun edit(next: CustomCleanupLevel) =
            onChange(
                settings.withCustomChoices(
                    levels =
                        settings.customCleanupLevels.map { if (it.id == level.id) next else it }
                )
            )
        CustomChoice(
            level.name,
            level.instructions,
            "Instructions",
            onEdit = { name, text -> edit(level.copy(name = name, instructions = text)) },
            onDelete = {
                onChange(settings.withCustomChoices(levels = settings.customCleanupLevels - level))
            },
        ) {
            DropdownRow("Builds on", baseLabels, level.base) { edit(level.copy(base = it)) }
        }
    }
    // A new level builds on Medium; its base can be changed once it is added.
    AddChoice("Add cleanup level", "Instructions", cleanupChoices(settings).values) {
        name,
        instructions ->
        val id = customChoiceId(name, settings.customCleanupLevels.map { it.id })
        onChange(
            settings.withCustomChoices(
                levels =
                    settings.customCleanupLevels +
                        CustomCleanupLevel(id, name, CleanupStrength.Balanced, instructions)
            )
        )
    }
}

@Composable
internal fun CustomPromptSettings(
    settings: SpeecherSettings,
    onChange: (SpeecherSettings) -> Unit,
) {
    ListItem(
        headlineContent = { Text("Custom system prompt") },
        supportingContent = {
            Text("Replaces the built-in dictation rules and cleanup levels with your own prompt.")
        },
        trailingContent = {
            Switch(
                settings.customSystemPromptEnabled,
                { onChange(settings.copy(customSystemPromptEnabled = it)) },
            )
        },
        colors = rowColors(),
    )
    if (!settings.customSystemPromptEnabled) return
    OutlinedTextField(
        settings.customSystemPrompt.ifEmpty { builtInDictationSystemPrompt },
        { onChange(settings.copy(customSystemPrompt = it)) },
        Modifier.fillMaxWidth().padding(horizontal = 16.dp),
        label = { Text("Prompt") },
        minLines = 4,
        maxLines = 6,
    )
    TextButton(
        { onChange(settings.copy(customSystemPrompt = builtInDictationSystemPrompt)) },
        Modifier.padding(horizontal = 4.dp),
    ) {
        Text("Reset to built-in")
    }
}

/** One line of grey text that says what a page holds, under its top bar. */
@Composable
private fun PageIntro(text: String) {
    Text(
        text,
        Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
        style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

/**
 * One custom tone or level: its name and its instructions, editable, with a delete button and
 * [extra] rows under the name.
 */
@Composable
private fun CustomChoice(
    name: String,
    text: String,
    textLabel: String,
    onEdit: (name: String, text: String) -> Unit,
    onDelete: () -> Unit,
    extra: @Composable () -> Unit = {},
) {
    Row(
        Modifier.padding(start = 16.dp, end = 4.dp, top = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        OutlinedTextField(
            name,
            { onEdit(it, text) },
            Modifier.weight(1f),
            label = { Text("Name") },
            singleLine = true,
            isError = name.isBlank(),
        )
        IconButton(onDelete) {
            Icon(painterResource(R.drawable.ic_close), contentDescription = "Delete $name")
        }
    }
    extra()
    OutlinedTextField(
        text,
        { onEdit(name, it) },
        Modifier.fillMaxWidth().padding(horizontal = 16.dp),
        label = { Text(textLabel) },
        minLines = 2,
    )
}

/**
 * [action] as a button that opens a bottom sheet holding [form]. The form gets the call that closes
 * the sheet, for its own Cancel and Add.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun AddInSheet(action: String, form: @Composable ColumnScope.(close: () -> Unit) -> Unit) {
    var open by rememberSaveable { mutableStateOf(false) }
    FilledTonalButton({ open = true }, Modifier.padding(16.dp)) { Text(action) }
    if (!open) return
    val close = { open = false }
    ModalBottomSheet(close, sheetState = rememberModalBottomSheetState(true)) {
        Column(
            Modifier.verticalScroll(rememberScrollState()).padding(bottom = 16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(
                action,
                Modifier.padding(horizontal = 16.dp),
                style = MaterialTheme.typography.headlineSmall,
            )
            form(close)
        }
    }
}

/** Cancel and the sheet's [confirm] action, which is enabled once the form [canConfirm]. */
@Composable
internal fun SheetActions(
    canConfirm: Boolean,
    onCancel: () -> Unit,
    confirm: String = "Add",
    onConfirm: () -> Unit,
) {
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 16.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.End),
    ) {
        TextButton(onCancel) { Text("Cancel") }
        Button(onConfirm, enabled = canConfirm) { Text(confirm) }
    }
}

/**
 * A name and instructions for a new profile, tone or level, added once the name is set and unlike
 * [taken], and the instructions are set when [needsText]. The id comes from the name given here.
 */
@Composable
private fun AddChoice(
    action: String,
    textLabel: String,
    taken: Collection<String>,
    needsText: Boolean = false,
    onAdd: (name: String, text: String) -> Unit,
) {
    AddInSheet(action) { close ->
        var name by rememberSaveable { mutableStateOf("") }
        var text by rememberSaveable { mutableStateOf("") }
        val duplicate = taken.any { it.equals(name.trim(), ignoreCase = true) }
        OutlinedTextField(
            name,
            { name = it },
            Modifier.fillMaxWidth().padding(horizontal = 16.dp),
            label = { Text("Name") },
            singleLine = true,
            isError = duplicate,
            supportingText = if (duplicate) ({ Text("That name is taken.") }) else null,
        )
        OutlinedTextField(
            text,
            { text = it },
            Modifier.fillMaxWidth().padding(horizontal = 16.dp),
            label = { Text(textLabel) },
            minLines = 2,
            maxLines = 6,
        )
        SheetActions(
            name.isNotBlank() && !duplicate && (!needsText || text.isNotBlank()),
            close,
        ) {
            onAdd(name.trim(), text)
            close()
        }
    }
}

/**
 * Deletes a custom profile. When a rule or the fallback points at it, the first tap shows what the
 * delete changes and asks again, as the desktop's confirmation does.
 */
@Composable
private fun DeleteProfile(notice: String, onDelete: () -> Unit) {
    var confirming by remember { mutableStateOf(false) }
    if (confirming) {
        Text(
            notice,
            Modifier.padding(horizontal = 16.dp),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
    Row(Modifier.padding(horizontal = 4.dp)) {
        if (confirming) TextButton({ confirming = false }) { Text("Cancel") }
        TextButton({ if (notice.isEmpty() || confirming) onDelete() else confirming = true }) {
            Text("Delete profile")
        }
    }
}

/** The app type and profile a rule sets, as one line. */
private fun ruleSummary(rule: RecognitionRule, profiles: Map<WritingProfile, String>): String =
    listOfNotNull(rule.category?.let { appTypeLabels[it] }, rule.profile?.let { profiles[it] })
        .joinToString(" · ")

/** The desktop's built-in rules, read-only and folded away until asked for. */
@Composable
private fun BuiltInRules(profiles: Map<WritingProfile, String>) {
    var shown by rememberSaveable { mutableStateOf(false) }
    TextButton({ shown = !shown }, Modifier.padding(horizontal = 4.dp)) {
        Text(if (shown) "Hide built-in rules" else "Show ${builtInRules.size} built-in rules")
    }
    if (shown) {
        builtInRules.forEach { rule ->
            ListItem(
                headlineContent = { Text(rule.match) },
                supportingContent = { Text(ruleSummary(rule, profiles)) },
                colors = rowColors(),
            )
        }
    }
}

/** One of the user's rules: its match text, app type and profile, editable, and delete. */
@Composable
private fun RuleEditor(
    rule: RecognitionRule,
    appTypes: Map<AppCategory?, String>,
    profiles: Map<WritingProfile?, String>,
    onEdit: (RecognitionRule) -> Unit,
    onDelete: () -> Unit,
) {
    Row(
        Modifier.padding(start = 16.dp, end = 4.dp, top = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        OutlinedTextField(
            rule.match,
            { onEdit(rule.copy(match = it)) },
            Modifier.weight(1f),
            label = { Text("App name or ID contains") },
            singleLine = true,
            isError = rule.match.isBlank(),
        )
        IconButton(onDelete) {
            Icon(painterResource(R.drawable.ic_close), contentDescription = "Delete ${rule.match}")
        }
    }
    DropdownRow("App type", appTypes, rule.category) { onEdit(rule.copy(category = it)) }
    DropdownRow("Profile", profiles, rule.profile) { onEdit(rule.copy(profile = it)) }
}

/** A new rule, added once it has a match text and sets an app type or a profile. */
@Composable
private fun AddRule(
    appTypes: Map<AppCategory?, String>,
    profiles: Map<WritingProfile?, String>,
    onAdd: (RecognitionRule) -> Unit,
) {
    AddInSheet("Add rule") { close ->
        var match by rememberSaveable { mutableStateOf("") }
        var category by remember { mutableStateOf<AppCategory?>(null) }
        var profile by remember { mutableStateOf<WritingProfile?>(null) }
        OutlinedTextField(
            match,
            { match = it },
            Modifier.fillMaxWidth().padding(horizontal = 16.dp),
            label = { Text("App name or ID contains") },
            singleLine = true,
        )
        DropdownRow("App type", appTypes, category) { category = it }
        DropdownRow("Profile", profiles, profile) { profile = it }
        SheetActions(match.isNotBlank() && (category != null || profile != null), close) {
            onAdd(RecognitionRule(match.trim(), category, profile))
            close()
        }
    }
}
