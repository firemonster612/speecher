package app.speecher.android.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Card
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.dictation.DictationRecord
import app.speecher.android.dictation.InsightTile
import app.speecher.android.dictation.InsightsRange
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.android.dictation.insightTiles
import app.speecher.android.dictation.summarize
import java.time.LocalDate
import java.time.LocalDateTime

/**
 * The stats kept of inserted dictations, for a chosen period, and the controls over keeping them.
 * [onClear] deletes the history and says whether it could.
 */
@Composable
fun Insights(
    records: List<DictationRecord>,
    settings: SpeecherSettings,
    onChange: (SpeecherSettings) -> Unit,
    onClear: () -> Boolean,
    today: LocalDate = LocalDate.now(),
) {
    when {
        !settings.insightsEnabled ->
            Notice(
                "Insights are off",
                "Speecher isn't recording new dictation. History you already have stays on this " +
                    "phone until you clear it in Insights settings.",
            )
        records.isEmpty() ->
            Notice(
                "No insights yet",
                "Your stats appear here after your next dictation. They're stored only on this " +
                    "phone and never sent to the cloud.",
            )
        else -> Stats(records, today)
    }
    Section("Insights settings")
    ListItem(
        headlineContent = { Text("Keep insights about your dictation") },
        supportingContent = {
            Text("Word counts, times and app names, never text or audio. Stays on this phone.")
        },
        trailingContent = {
            Switch(settings.insightsEnabled, { onChange(settings.copy(insightsEnabled = it)) })
        },
        colors = rowColors(),
    )
    ClearHistory(onClear)
}

@Composable
private fun Notice(title: String, body: String) {
    Card(Modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, top = 16.dp)) {
        Column(Modifier.padding(16.dp)) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            Text(body, style = MaterialTheme.typography.bodyMedium)
        }
    }
}

@Composable
private fun Stats(records: List<DictationRecord>, today: LocalDate) {
    var range by rememberSaveable { mutableStateOf(InsightsRange.Last30Days) }
    Section("Your dictation")
    DropdownRow("Period", InsightsRange.entries.associateWith { it.label }, range) { range = it }
    Column(Modifier.padding(horizontal = 16.dp), Arrangement.spacedBy(8.dp)) {
        insightTiles(summarize(records, range, today)).chunked(2).forEach { pair ->
            Row(Modifier.height(IntrinsicSize.Min), Arrangement.spacedBy(8.dp)) {
                pair.forEach { Tile(it, Modifier.weight(1f).fillMaxHeight()) }
            }
        }
    }
    Text(
        "Insights are stored only on this phone and are never sent to the cloud.",
        Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

@Composable
private fun Tile(tile: InsightTile, modifier: Modifier) {
    Card(modifier) {
        Column(Modifier.padding(16.dp)) {
            Text(
                tile.title,
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Row {
                Text(
                    tile.value,
                    Modifier.alignByBaseline(),
                    style = MaterialTheme.typography.headlineMedium,
                )
                if (tile.unit.isNotEmpty()) {
                    Text(
                        tile.unit,
                        Modifier.alignByBaseline().padding(start = 4.dp),
                        style = MaterialTheme.typography.bodyLarge,
                    )
                }
            }
            tile.lines.forEach {
                Text(
                    it,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

/** Asks before deleting, and keeps asking with the reason when the delete fails. */
@Composable
private fun ClearHistory(onClear: () -> Boolean) {
    var confirming by remember { mutableStateOf(false) }
    var failed by remember { mutableStateOf(false) }
    ListItem(
        headlineContent = { Text("Clear insights history") },
        supportingContent = { Text("Delete every recorded dictation from this phone.") },
        modifier = Modifier.clickable { confirming = true },
        colors = rowColors(),
    )
    if (!confirming) return
    Column(Modifier.padding(horizontal = 16.dp)) {
        Text("Delete all insights history?", style = MaterialTheme.typography.bodyLarge)
        Text(
            "Your stats are erased from this phone. This can't be undone.",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        if (failed) {
            Text(
                "Speecher couldn't delete the insights history. Try again.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.error,
            )
        }
    }
    Row(Modifier.padding(horizontal = 4.dp)) {
        TextButton({
            confirming = false
            failed = false
        }) {
            Text("Cancel")
        }
        TextButton({
            failed = !onClear()
            confirming = failed
        }) {
            Text("Delete history")
        }
    }
}

private val previewToday = LocalDate.of(2026, 10, 5)

private val previewRecords =
    listOf(
        DictationRecord(LocalDateTime.of(2026, 10, 5, 9, 12), 42_000, 118, "Messages", "personal"),
        DictationRecord(LocalDateTime.of(2026, 10, 4, 18, 40), 95_000, 241, "Gmail", "email"),
        DictationRecord(LocalDateTime.of(2026, 10, 2, 11, 5), 61_000, 160, "Slack", "work"),
        DictationRecord(LocalDateTime.of(2026, 9, 25, 8, 30), 120_000, 300, "Gmail", "email"),
    )

@Composable
private fun InsightsPreview(
    records: List<DictationRecord>,
    enabled: Boolean = true,
    onClear: () -> Boolean = { true },
) = SpeecherTheme {
    SpeecherScreen("Insights", onBack = {}) {
        Insights(records, SpeecherSettings(insightsEnabled = enabled), {}, onClear, previewToday)
    }
}

@PreviewLightDark @Composable internal fun InsightsStatsPreview() = InsightsPreview(previewRecords)

@PreviewLightDark @Composable internal fun InsightsEmptyPreview() = InsightsPreview(emptyList())

@PreviewLightDark
@Composable
internal fun InsightsOffPreview() = InsightsPreview(previewRecords, enabled = false)

/** Clearing fails, to show the failure under the confirmation. */
@PreviewLightDark
@Composable
internal fun InsightsClearFailsPreview() = InsightsPreview(previewRecords, onClear = { false })
