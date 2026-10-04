package app.speecher.android.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
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
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.android.update.IntervalUnit
import app.speecher.android.update.fittingUnit

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

/** The Updates section's row: a preset from the menu, or Custom… to enter any interval. */
@Composable
internal fun UpdateCheckRow(settings: SpeecherSettings, onChange: (SpeecherSettings) -> Unit) {
    var editing by rememberSaveable { mutableStateOf(false) }
    val minutes = settings.updateCheckMinutes
    val customUnit = settings.updateCheckUnit
    DropdownRow<Int?>(
        "Check for updates",
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
