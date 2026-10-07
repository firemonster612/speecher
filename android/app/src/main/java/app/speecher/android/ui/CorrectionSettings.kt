package app.speecher.android.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.R
import app.speecher.android.dictation.SettingsStore
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.android.dictation.targetApp
import app.speecher.protocol.LearnedCorrection
import kotlin.math.roundToInt

/**
 * The desktop's Learned corrections page: whether to learn, and each correction learned, newest
 * first, to turn off or remove. Learning needs the dictation button, the accessibility service that
 * reads the field after an insertion; [onTurnOnChip] opens the place to turn it on. Changes to the
 * corrections go straight to the store, so one learned while the page is open is kept; the store's
 * listener brings them back in [settings].
 */
@Composable
internal fun CorrectionSettings(
    settings: SpeecherSettings,
    chipEnabled: Boolean,
    onTurnOnChip: () -> Unit,
    onChange: (SpeecherSettings) -> Unit,
) {
    val store = SettingsStore(LocalContext.current)
    val colors = rowColors()
    ListItem(
        headlineContent = {
            Text(
                "Learn corrections",
                color = if (chipEnabled) colors.headlineColor else colors.disabledHeadlineColor,
            )
        },
        supportingContent = {
            Column {
                Text(
                    "After inserting text, briefly watch for your edits to it and learn repeated " +
                        "corrections."
                )
                if (!chipEnabled) {
                    Text(
                        "Turn on the dictation button to learn corrections.",
                        color = MaterialTheme.colorScheme.onSurface,
                    )
                    TextButton(onTurnOnChip) { Text("Turn on") }
                }
            }
        },
        trailingContent = {
            Switch(
                settings.correctionLearningEnabled,
                { onChange(settings.copy(correctionLearningEnabled = it)) },
                enabled = chipEnabled,
            )
        },
        colors = colors,
    )
    Text(
        "Turn a correction off to stop using it.",
        Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
        style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
    if (settings.learnedCorrections.isEmpty())
        ListItem(
            headlineContent = { Text("No learned corrections yet") },
            supportingContent = {
                Text(
                    "When you fix a dictated word the same way more than once, the correction " +
                        "appears here."
                )
            },
            colors = colors,
        )
    val packages = LocalContext.current.packageManager
    settings.learnedCorrections.forEach { correction ->
        val app =
            remember(correction.applicationId) {
                if (correction.applicationId.isEmpty()) "All apps"
                else
                    targetApp(correction.applicationId, packages, false).label.ifEmpty {
                        correction.applicationId
                    }
            }
        ListItem(
            headlineContent = { Text(correction.corrected) },
            supportingContent = {
                Column {
                    Text("Heard “${correction.original}” · $app")
                    Text(
                        "Learned automatically · confidence " +
                            "${(correction.confidence * 100).roundToInt()}%"
                    )
                }
            },
            trailingContent = {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Switch(
                        correction.enabled,
                        { enabled ->
                            store.editCorrections { stored ->
                                stored.map {
                                    if (it.id == correction.id) it.copy(enabled = enabled) else it
                                }
                            }
                        },
                    )
                    IconButton({
                        store.editCorrections { stored -> stored.filter { it.id != correction.id } }
                    }) {
                        Icon(
                            painterResource(R.drawable.ic_close),
                            contentDescription = "Remove ${correction.corrected}",
                        )
                    }
                }
            },
            colors = colors,
        )
    }
    // The newest is first, so undoing the last correction learned removes the top one.
    OutlinedButton(
        { store.editCorrections { it.drop(1) } },
        Modifier.padding(16.dp),
        enabled = settings.learnedCorrections.isNotEmpty(),
    ) {
        Text("Undo last correction")
    }
}

private val previewCorrections =
    listOf(
        LearnedCorrection("1", "speaker", "Speecher", "com.Slack", 1, 0.75, evidenceCount = 2),
        LearnedCorrection("2", "github", "GitHub", "", 1, 0.98),
        LearnedCorrection("3", "cube control", "kubectl", "com.termux", 1, 0.75, enabled = false),
    )

@PreviewLightDark
@Composable
internal fun CorrectionSettingsPreview() = SpeecherTheme {
    Column {
        CorrectionSettings(
            SpeecherSettings(learnedCorrections = previewCorrections),
            chipEnabled = true,
            onTurnOnChip = {},
            onChange = {},
        )
    }
}

@PreviewLightDark
@Composable
internal fun CorrectionSettingsChipOffPreview() = SpeecherTheme {
    Column {
        CorrectionSettings(
            SpeecherSettings(),
            chipEnabled = false,
            onTurnOnChip = {},
            onChange = {},
        )
    }
}
