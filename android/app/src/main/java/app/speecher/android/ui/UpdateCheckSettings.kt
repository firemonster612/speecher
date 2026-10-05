package app.speecher.android.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.BuildConfig
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.android.update.ApkUpdate
import app.speecher.android.update.IntervalUnit
import app.speecher.android.update.UpdateChannel
import app.speecher.android.update.UpdateState
import app.speecher.android.update.fittingUnit
import app.speecher.android.update.versionDisplay

/** The menu's intervals, in minutes. */
private val updateCheckPresets =
    mapOf(
        5 to "Every 5 minutes",
        15 to "Every 15 minutes",
        30 to "Every 30 minutes",
        60 to "Every hour",
        360 to "Every 6 hours",
        720 to "Every 12 hours",
        1_440 to "Every day",
        10_080 to "Every week",
    )

/**
 * How the row shows an interval: a preset by its name, a custom one in the unit it was entered in,
 * with no number for one of it.
 */
internal fun updateCheckLabel(minutes: Int, customUnit: IntervalUnit?): String {
    if (customUnit == null)
        updateCheckPresets[minutes]?.let {
            return it
        }
    val unit = customUnit ?: fittingUnit(minutes)
    val count = minutes / unit.minutes
    val noun =
        when (unit) {
            IntervalUnit.Minutes -> "minute"
            IntervalUnit.Hours -> "hour"
            IntervalUnit.Days -> "day"
        }
    return if (count == 1) "Every $noun" else "Every $count ${noun}s"
}

/** What the Check for updates row offers and says. */
internal data class CheckRow(val caption: String, val help: String, val enabled: Boolean = true)

/** The Check for updates row in [state], worded as on the desktop. */
internal fun checkRow(state: UpdateState, channel: UpdateChannel): CheckRow =
    when (state) {
        UpdateState.Idle ->
            CheckRow("Check now", "Check the ${channel.feed} feed for a newer build.")
        UpdateState.Checking -> CheckRow("Checking…", "Checking the ${channel.feed} feed.", false)
        UpdateState.UpToDate -> CheckRow("Check again", "Speecher is up to date.")
        UpdateState.CheckFailed -> CheckRow("Try again", "Update check failed.")
        is UpdateState.Available -> CheckRow("Update now", "${availableText(state.update)}.")
        is UpdateState.Downloading -> CheckRow("Downloading…", downloadingText(state), false)
        is UpdateState.WaitingForDictation ->
            CheckRow("Installing…", INSTALLING_AFTER_DICTATION, false)
        is UpdateState.Installing -> CheckRow("Installing…", installingText(state), false)
        is UpdateState.InstallFailed -> CheckRow(retryCaption(state), state.message)
    }

internal fun availableText(update: ApkUpdate) =
    if (update.replacesNightly)
        "Switch to Stable Release ${update.version} (replaces this Nightly Build)"
    else "Speecher ${versionDisplay(update.version)} is available"

internal fun installingText(state: UpdateState.Installing) =
    "Installing Speecher ${versionDisplay(state.update.version)}…"

internal fun downloadingText(state: UpdateState.Downloading): String =
    "Downloading Speecher ${versionDisplay(state.update.version)}" +
        (state.percent?.let { " ($it%)" } ?: "…")

internal const val INSTALLING_AFTER_DICTATION = "Installing after this dictation…"

/** Trying again, or opening the release page when only installing from there can work. */
internal fun retryCaption(state: UpdateState.InstallFailed) =
    if (state.manualInstall) "Open release page" else "Try again"

/**
 * The Settings list's Updates section, as on the desktop: the channel, automatic checks and how
 * often, Check now with where things stand, the installed version, and What's New.
 */
@Composable
internal fun UpdatesSection(
    settings: SpeecherSettings,
    onChange: (SpeecherSettings) -> Unit,
    updateState: UpdateState,
    onCheckRow: () -> Unit,
    onOpenWhatsNew: () -> Unit,
) {
    Section("Updates")
    DropdownRow(
        "Update channel",
        UpdateChannel.entries.associateWith { it.label },
        settings.updateChannel,
        description = settings.updateChannel.description,
    ) {
        onChange(settings.copy(updateChannel = it))
    }
    ListItem(
        headlineContent = { Text("Check for updates automatically") },
        trailingContent = {
            Switch(settings.autoCheckUpdates, { onChange(settings.copy(autoCheckUpdates = it)) })
        },
        colors = rowColors(),
    )
    if (settings.autoCheckUpdates) UpdateCheckRow(settings, onChange)
    val row = checkRow(updateState, settings.updateChannel)
    ListItem(
        headlineContent = { Text("Check for updates") },
        supportingContent = { Text(row.help) },
        trailingContent = { TextButton(onCheckRow, enabled = row.enabled) { Text(row.caption) } },
        colors = rowColors(),
    )
    ListItem(
        headlineContent = { Text("Current version") },
        trailingContent = {
            Text(BuildConfig.VERSION_NAME, style = MaterialTheme.typography.bodyLarge)
        },
        colors = rowColors(),
    )
    ListItem(
        headlineContent = { Text("What's New") },
        supportingContent = { Text("Release notes for this version.") },
        trailingContent = { Chevron() },
        modifier = Modifier.clickable(onClick = onOpenWhatsNew),
        colors = rowColors(),
    )
}

/** How often automatic checks run: a preset from the menu, or Custom… to enter any interval. */
@Composable
private fun UpdateCheckRow(settings: SpeecherSettings, onChange: (SpeecherSettings) -> Unit) {
    var editing by rememberSaveable { mutableStateOf(false) }
    val minutes = settings.updateCheckMinutes
    val customUnit = settings.updateCheckUnit
    DropdownRow<Int?>(
        "Check frequency",
        updateCheckPresets + (null to "Custom…"),
        minutes,
        selectedLabel = updateCheckLabel(minutes, customUnit),
    ) { chosen ->
        if (chosen == null) editing = true
        else onChange(settings.copy(updateCheckMinutes = chosen, updateCheckUnit = null))
    }
    if (editing) {
        CustomIntervalDialog(
            minutes,
            customUnit ?: fittingUnit(minutes),
            onDismiss = { editing = false },
            onSave = { chosenMinutes, unit ->
                onChange(settings.copy(updateCheckMinutes = chosenMinutes, updateCheckUnit = unit))
                editing = false
            },
        )
    }
}

/**
 * A count and a unit, opened at [minutes] in [unit]; OK stays off while the count is out of range.
 */
@Composable
private fun CustomIntervalDialog(
    minutes: Int,
    unit: IntervalUnit,
    onDismiss: () -> Unit,
    onSave: (minutes: Int, unit: IntervalUnit) -> Unit,
) {
    var text by rememberSaveable { mutableStateOf((minutes / unit.minutes).toString()) }
    var chosen by rememberSaveable { mutableStateOf(unit) }
    val count = text.toIntOrNull()?.takeIf { it in chosen.counts }
    AlertDialog(
        onDismissRequest = onDismiss,
        confirmButton = {
            TextButton(
                { count?.let { onSave(it * chosen.minutes, chosen) } },
                enabled = count != null,
            ) {
                Text("OK")
            }
        },
        dismissButton = { TextButton(onDismiss) { Text("Cancel") } },
        title = { Text("Custom interval") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
                OutlinedTextField(
                    text,
                    { text = it },
                    Modifier.fillMaxWidth(),
                    label = { Text("Every") },
                    supportingText = { Text("Between 5 minutes and 30 days.") },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    singleLine = true,
                )
                SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                    IntervalUnit.entries.forEachIndexed { index, option ->
                        SegmentedButton(
                            selected = option == chosen,
                            onClick = { chosen = option },
                            shape =
                                SegmentedButtonDefaults.itemShape(index, IntervalUnit.entries.size),
                        ) {
                            Text(option.name, maxLines = 1)
                        }
                    }
                }
            }
        },
    )
}

@PreviewLightDark
@Composable
internal fun CustomIntervalDialogPreview() = SpeecherTheme {
    CustomIntervalDialog(480, IntervalUnit.Hours, {}) { _, _ -> }
}

@PreviewLightDark
@Composable
internal fun UpdatesSectionPreview() = SpeecherTheme {
    Surface {
        Column(Modifier.statusBarsPadding()) {
            UpdatesSection(
                SpeecherSettings(updateChannel = UpdateChannel.Nightly),
                {},
                UpdateState.Downloading(
                    previewUpdate.copy(version = "0.2.1-nightly.57+g1a2b3c4"),
                    42,
                ),
                {},
                {},
            )
            UpdatesSection(
                SpeecherSettings(autoCheckUpdates = false),
                {},
                UpdateState.CheckFailed,
                {},
                {},
            )
        }
    }
}

internal val previewUpdate =
    ApkUpdate("0.3.0", "https://example.com/speecher.apk", "https://example.com/release")
