package app.speecher.android.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.update.AndroidRelease
import app.speecher.android.update.versionDisplay

internal enum class NoteKind {
    Heading,
    Bullet,
    Text,
}

internal data class NoteLine(val kind: NoteKind, val text: String)

/**
 * Release notes Markdown as the page shows it: headings, bullets and paragraphs, with hard-wrapped
 * lines joined onto the bullet or paragraph they continue.
 */
internal fun noteLines(markdown: String): List<NoteLine> {
    val lines = mutableListOf<NoteLine>()
    var continues = false
    for (raw in markdown.lines()) {
        val line = raw.trim()
        val last = lines.lastOrNull()
        when {
            line.isEmpty() -> continues = false
            line.startsWith("#") -> lines += NoteLine(NoteKind.Heading, line.trimStart('#').trim())
            line.startsWith("- ") || line.startsWith("* ") ->
                lines += NoteLine(NoteKind.Bullet, line.drop(2))
            continues && last != null && last.kind != NoteKind.Heading ->
                lines[lines.lastIndex] = last.copy(text = "${last.text} $line")
            else -> lines += NoteLine(NoteKind.Text, line)
        }
        if (line.isNotEmpty()) continues = true
    }
    return lines
}

/**
 * Release notes for the installed version and those since the last one seen; null while they load.
 * [onViewReleases] adds a link to every release, for a build without notes of its own.
 */
@Composable
fun WhatsNew(
    notes: Result<List<AndroidRelease>>?,
    onRetry: () -> Unit,
    onViewReleases: (() -> Unit)? = null,
) {
    val padding = Modifier.padding(horizontal = 16.dp, vertical = 4.dp)
    when {
        notes == null ->
            Row(padding) {
                CircularProgressIndicator()
                Text("Loading release notes…", Modifier.padding(start = 16.dp, top = 8.dp))
            }
        notes.isFailure ->
            Column(padding) {
                Text(
                    "Couldn't load the release notes. Check your connection and try again.",
                    color = MaterialTheme.colorScheme.error,
                )
                Button(onRetry, Modifier.padding(top = 8.dp)) { Text("Try again") }
            }
        notes.getOrThrow().isEmpty() -> Text("No release notes for this version yet.", padding)
        else ->
            notes.getOrThrow().forEach { release ->
                Section("Speecher ${versionDisplay(release.version)}")
                noteLines(release.notes).forEach { NoteRow(it, padding) }
            }
    }
    if (notes?.isSuccess == true && onViewReleases != null) {
        TextButton(onViewReleases, Modifier.padding(horizontal = 4.dp)) { Text("View releases") }
    }
}

@Composable
private fun NoteRow(line: NoteLine, modifier: Modifier) {
    when (line.kind) {
        NoteKind.Heading ->
            Text(
                line.text,
                modifier.padding(top = 8.dp),
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.Bold,
            )
        NoteKind.Bullet ->
            Row(modifier) {
                Text("•", Modifier.padding(end = 8.dp))
                Text(line.text)
            }
        NoteKind.Text -> Text(line.text, modifier)
    }
}

@PreviewLightDark
@Composable
internal fun WhatsNewPreview() = WhatsNewPreview(onViewReleases = null)

/** On a Nightly Build: the newest Stable Release's notes, and a link to every release. */
@PreviewLightDark
@Composable
internal fun WhatsNewNightlyPreview() = WhatsNewPreview(onViewReleases = {})

@Composable
private fun WhatsNewPreview(onViewReleases: (() -> Unit)?) = SpeecherTheme {
    SpeecherScreen("What's New", onBack = {}) {
        WhatsNew(
            Result.success(
                listOf(
                    AndroidRelease(
                        "0.3.0",
                        """
                        ## Added

                        - Check for updates from Settings, see the download's progress, and read
                          these notes after an update.
                        - A Nightly update channel with a build of every push to master.

                        ## Fixed

                        - An update no longer installs in the middle of a dictation.
                        """
                            .trimIndent(),
                        "",
                        null,
                    )
                )
            ),
            {},
            onViewReleases,
        )
    }
}

@PreviewLightDark
@Composable
internal fun WhatsNewFailedPreview() = SpeecherTheme {
    SpeecherScreen("What's New", onBack = {}) {
        WhatsNew(Result.failure(IllegalStateException("offline")), {})
    }
}
