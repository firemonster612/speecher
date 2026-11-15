package app.speecher.android.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
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
import app.speecher.android.settings.SpeecherSettings
import app.speecher.android.settings.profileDeletionNotice
import app.speecher.protocol.AppCategory
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.RecognitionRule
import app.speecher.protocol.RefinementRule
import app.speecher.protocol.RuleGroup
import app.speecher.protocol.Tone
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import app.speecher.protocol.builtInDictationSystemPrompt
import app.speecher.protocol.builtInProfileLabels
import app.speecher.protocol.builtInRefinementRules
import app.speecher.protocol.builtInRules
import app.speecher.protocol.changedBuiltInRules
import app.speecher.protocol.customChoiceId
import app.speecher.protocol.newRuleId
import app.speecher.protocol.ruleOn
import app.speecher.protocol.withRule
import app.speecher.protocol.withoutBuiltInRules

// The app types, in the order the desktop's rules offer them.
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

private val toneLabels =
    mapOf(
        Tone.None to "No tone override",
        Tone.Formal to "Formal",
        Tone.Casual to "Casual",
        Tone.VeryCasual to "Very casual",
        Tone.Excited to "Excited",
        Tone.GenZ to "Gen Z",
    )

/** Every profile, by id: the built-ins, then the custom ones by name. */
internal fun profileChoices(settings: SpeecherSettings): Map<WritingProfile, String> =
    builtInProfileLabels +
        settings.writingProfiles.filterKeys { !it.isBuiltIn }.mapValues { it.value.name }

/** The built-in rules' labels, by id, in the order the profile screen lists them. */
internal val builtInRuleLabels =
    mapOf(
        "fillers" to "Remove filler words and false starts",
        "smoothing" to "Smooth clumsy phrasing",
        "structure" to "Add paragraphs and lists",
        "rewrite" to "Rewrite for clarity and flow",
        "headings" to "Add short headings",
        "digits" to "Write numbers, times and dates as digits",
        "markdown" to "Use light Markdown",
        "noEmDashes" to "Avoid em dashes",
        "codingAgent" to "Write as a prompt for a coding agent",
        "britishSpelling" to "Use British spelling",
        "lowercase" to "Write in lowercase",
    )

private val ruleGroupLabels =
    mapOf(
        RuleGroup.Cleanup to "Cleanup",
        RuleGroup.Formatting to "Formatting",
        RuleGroup.More to "More",
    )

@Composable
internal fun ProfileSettings(settings: SpeecherSettings, onChange: (SpeecherSettings) -> Unit) {
    PageIntro(
        "Choose a cleanup level, a tone and the rules for each profile. An output language " +
            "translates what you say, with at least Light cleanup.",
    )
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
        val level = cleanupLabels.getValue(behavior.cleanupStrength)
        val changed = behavior.changedBuiltInRules
        // Choosing a level starts its built-in rules afresh, as on the desktop.
        DropdownRow(
            "Cleanup",
            cleanupLabels,
            behavior.cleanupStrength,
            selectedLabel = if (changed == 0) level else "$level, $changed changed",
        ) {
            if (it != behavior.cleanupStrength)
                update(behavior.copy(cleanupStrength = it).withoutBuiltInRules())
        }
        if (changed > 0) {
            TextButton(
                { update(behavior.withoutBuiltInRules()) },
                Modifier.padding(horizontal = 4.dp),
            ) {
                Text("Reset to $level")
            }
        }
        DropdownRow("Tone", toneLabels, behavior.tone) { update(behavior.copy(tone = it)) }
        OutlinedTextField(
            behavior.outputLanguage,
            { update(behavior.copy(outputLanguage = it)) },
            Modifier.fillMaxWidth().padding(start = 16.dp, top = 8.dp, end = 16.dp),
            label = { Text("Output language") },
            placeholder = { Text("Same as spoken") },
            singleLine = true,
        )
        RuleSwitches(settings, profile, behavior, ::update)
        if (!profile.isBuiltIn) {
            DeleteProfile(settings.profileDeletionNotice(profile)) {
                onChange(settings.withWritingProfiles(settings.writingProfiles - profile))
            }
        }
    }
    AddInSheet("Add profile") { close ->
        var name by rememberSaveable { mutableStateOf("") }
        NameField(name, profiles.values) { name = it }
        SheetActions(nameFree(name, profiles.values), close) {
            val id = customChoiceId(name, settings.writingProfiles.keys.map { it.id })
            onChange(
                settings.copy(
                    writingProfiles =
                        settings.writingProfiles +
                            (WritingProfile(id) to WritingProfileSettings(name = name.trim())),
                ),
            )
            close()
        }
    }
}

/**
 * A switch per rule for [profile]: the built-in rules by group, then the person's own. None with no
 * output language refines nothing, so every switch is off limits; a custom system prompt replaces
 * the built-in rules, so theirs are.
 */
@Composable
private fun RuleSwitches(
    settings: SpeecherSettings,
    profile: WritingProfile,
    behavior: WritingProfileSettings,
    onChange: (WritingProfileSettings) -> Unit,
) {
    val rules = settings.refinementRules
    val refines =
        behavior.cleanupStrength != CleanupStrength.None || behavior.outputLanguage.isNotBlank()
    @Composable
    fun Group(title: String) =
        Text(
            title,
            Modifier.padding(start = 16.dp, end = 16.dp, top = 16.dp),
            style = MaterialTheme.typography.labelLarge,
        )
    @Composable
    fun RuleRow(id: String, label: String, enabled: Boolean, help: String = "") {
        ListItem(
            headlineContent = { Text(label) },
            supportingContent = help.takeIf(String::isNotBlank)?.let { { Text(it) } },
            trailingContent = {
                Switch(
                    ruleOn(id, profile, behavior, behavior.cleanupStrength, rules),
                    { onChange(behavior.withRule(id, it, profile, rules)) },
                    enabled = enabled,
                )
            },
            colors = rowColors(),
        )
    }
    // A blank custom prompt leaves the built-in rules in place, as dictationSystemPrompt does.
    val customPrompt =
        settings.customSystemPromptEnabled && settings.customSystemPrompt.isNotBlank()
    if (!refines) PageIntro("Cleanup None skips refinement, so rules have no effect.")
    else if (customPrompt)
        PageIntro("The custom system prompt replaces the built-in rules. Yours still apply.")
    builtInRefinementRules
        .groupBy { it.group }
        .forEach { (group, inGroup) ->
            Group(ruleGroupLabels.getValue(group))
            inGroup.forEach {
                RuleRow(it.id, builtInRuleLabels.getValue(it.id), refines && !customPrompt)
            }
        }
    if (rules.isEmpty()) return
    Group("Yours")
    rules.forEach { RuleRow(it.id, it.name, refines, it.instruction.trim()) }
}

/**
 * The person's rules: each with its instruction and where it is on, edited and deleted here and
 * switched per profile on the Profiles page.
 */
@Composable
internal fun YourRuleSettings(settings: SpeecherSettings, onChange: (SpeecherSettings) -> Unit) {
    PageIntro(
        "A rule tells refinement what you want, such as \"Sign off with Best, Sam\". Turn it on " +
            "for every profile here, or for some on the Profiles page.",
    )
    val rules = settings.refinementRules
    var editing by rememberSaveable { mutableStateOf<String?>(null) }
    rules.forEach { rule ->
        ListItem(
            headlineContent = { Text(rule.name) },
            supportingContent = {
                Text(
                    listOf(rule.instruction.trim(), ruleReach(settings, rule))
                        .filter(String::isNotEmpty)
                        .joinToString("\n"),
                )
            },
            trailingContent = {
                IconButton({ onChange(settings.withRefinementRules(rules - rule)) }) {
                    Icon(
                        painterResource(R.drawable.ic_close),
                        contentDescription = "Delete ${rule.name}",
                    )
                }
            },
            modifier = Modifier.clickable { editing = rule.id },
            colors = rowColors(),
        )
    }
    rules
        .firstOrNull { it.id == editing }
        ?.let { rule ->
            FormSheet("Edit rule", { editing = null }) {
                RuleForm(rule, rules - rule, "Save", { editing = null }) { edited ->
                    onChange(
                        settings.copy(
                            refinementRules = rules.map { if (it.id == rule.id) edited else it },
                        ),
                    )
                }
            }
        }
    AddInSheet("Add rule") { close ->
        RuleForm(RefinementRule("", ""), rules, "Add", close) {
            onChange(
                settings.copy(
                    refinementRules = rules + it.copy(id = newRuleId(rules.map { it.id })),
                ),
            )
        }
    }
}

/** Where [rule] is on: "Every profile", "2 profiles" or "Off". */
private fun ruleReach(settings: SpeecherSettings, rule: RefinementRule): String {
    val on =
        settings.writingProfiles.count { (profile, behavior) ->
            ruleOn(rule.id, profile, behavior, behavior.cleanupStrength, settings.refinementRules)
        }
    return when (on) {
        0 -> "Off"
        settings.writingProfiles.size -> "Every profile"
        else -> count(on, "profile", "profiles")
    }
}

/**
 * [rule]'s name, instruction and whether it is on in every profile, saved through [onSave] once the
 * name is set and none of [others] has it.
 */
@Composable
private fun RuleForm(
    rule: RefinementRule,
    others: List<RefinementRule>,
    confirm: String,
    close: () -> Unit,
    onSave: (RefinementRule) -> Unit,
) {
    var name by rememberSaveable { mutableStateOf(rule.name) }
    var instruction by rememberSaveable { mutableStateOf(rule.instruction) }
    var everyProfile by rememberSaveable { mutableStateOf(rule.everyProfile) }
    val taken = others.map { it.name }
    NameField(name, taken) { name = it }
    OutlinedTextField(
        instruction,
        { instruction = it },
        Modifier.fillMaxWidth().padding(horizontal = 16.dp),
        label = { Text("Instruction") },
        supportingText = { Text("Optional. Without one, refinement is told the name.") },
        minLines = 2,
        maxLines = 6,
    )
    ListItem(
        headlineContent = { Text("On in every profile") },
        trailingContent = { Switch(everyProfile, { everyProfile = it }) },
    )
    SheetActions(nameFree(name, taken), close, confirm) {
        onSave(RefinementRule(rule.id, name.trim(), instruction.trim(), everyProfile))
        close()
    }
}

@Composable
internal fun AppRuleSettings(settings: SpeecherSettings, onChange: (SpeecherSettings) -> Unit) {
    PageIntro(
        "Built-in rules are read-only. Your rules come first and can set the app type, the " +
            "writing profile, or both.",
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
                            settings.appRules.mapIndexed { at, it ->
                                if (at == index) next else it
                            },
                    ),
                )
            },
            onDelete = {
                onChange(
                    settings.copy(
                        appRules = settings.appRules.filterIndexed { at, _ -> at != index },
                    ),
                )
            },
        )
    }
    AddRule(appTypes, ruleProfiles) { onChange(settings.copy(appRules = settings.appRules + it)) }
}

@Composable
internal fun CustomPromptSettings(
    settings: SpeecherSettings,
    onChange: (SpeecherSettings) -> Unit,
) {
    ListItem(
        headlineContent = { Text("Custom system prompt") },
        supportingContent = {
            Text(
                "Replaces the built-in dictation rules and cleanup levels with your own prompt. " +
                    "Tone and your rules still apply.",
            )
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

/** A bottom sheet titled [title] holding [form]. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun FormSheet(title: String, close: () -> Unit, form: @Composable ColumnScope.() -> Unit) {
    ModalBottomSheet(close, sheetState = rememberModalBottomSheetState(true)) {
        Column(
            Modifier.verticalScroll(rememberScrollState()).padding(bottom = 16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(
                title,
                Modifier.padding(horizontal = 16.dp),
                style = MaterialTheme.typography.headlineSmall,
            )
            form()
        }
    }
}

/**
 * [action] as a button that opens a [FormSheet] holding [form]. The form gets the call that closes
 * the sheet, for its own Cancel and Add.
 */
@Composable
private fun AddInSheet(action: String, form: @Composable ColumnScope.(close: () -> Unit) -> Unit) {
    var open by rememberSaveable { mutableStateOf(false) }
    FilledTonalButton({ open = true }, Modifier.padding(16.dp)) { Text(action) }
    if (open) FormSheet(action, { open = false }) { form { open = false } }
}

/** Whether [name] is set and none of [taken] goes by it, ignoring case. */
private fun nameFree(name: String, taken: Collection<String>) =
    name.isNotBlank() && taken.none { it.equals(name.trim(), ignoreCase = true) }

/** A name field that says so when one of [taken] already goes by the name. */
@Composable
private fun NameField(name: String, taken: Collection<String>, onChange: (String) -> Unit) {
    val duplicate = name.isNotBlank() && !nameFree(name, taken)
    OutlinedTextField(
        name,
        onChange,
        Modifier.fillMaxWidth().padding(horizontal = 16.dp),
        label = { Text("Name") },
        singleLine = true,
        isError = duplicate,
        supportingText = if (duplicate) ({ Text("That name is taken.") }) else null,
    )
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
