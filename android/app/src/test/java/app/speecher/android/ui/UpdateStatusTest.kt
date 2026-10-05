package app.speecher.android.ui

import android.content.pm.PackageInstaller
import app.speecher.android.update.ApkUpdate
import app.speecher.android.update.UpdateChannel
import app.speecher.android.update.UpdateState
import app.speecher.android.update.installFailure
import org.junit.Assert.assertEquals
import org.junit.Test

class UpdateStatusTest {
    private val update = ApkUpdate("0.3.0", "https://example.com/0.3.0.apk", "https://example.com")

    @Test
    fun `the Check for updates row says where things stand, as on the desktop`() {
        assertEquals(
            CheckRow("Check now", "Check the Nightly Build feed for a newer build."),
            checkRow(UpdateState.Idle, UpdateChannel.Nightly),
        )
        assertEquals(
            CheckRow("Checking…", "Checking the Stable Release feed.", false),
            checkRow(UpdateState.Checking, UpdateChannel.Stable),
        )
        assertEquals(
            CheckRow("Check again", "Speecher is up to date."),
            checkRow(UpdateState.UpToDate, UpdateChannel.Stable),
        )
        assertEquals(
            CheckRow("Update now", "Speecher 0.3.0 is available."),
            checkRow(UpdateState.Available(update), UpdateChannel.Stable),
        )
        assertEquals(
            CheckRow("Downloading…", "Downloading Speecher 0.3.0 (42%)", false),
            checkRow(UpdateState.Downloading(update, 42), UpdateChannel.Stable),
        )
        assertEquals(
            CheckRow("Installing…", "Installing after this dictation…", false),
            checkRow(UpdateState.WaitingForDictation(update), UpdateChannel.Stable),
        )
    }

    @Test
    fun `a refused install says why, and sends a signing conflict to the release page`() {
        assertEquals(
            CheckRow("Try again", "The update was canceled."),
            checkRow(
                installFailure(update, PackageInstaller.STATUS_FAILURE_ABORTED),
                UpdateChannel.Stable,
            ),
        )
        assertEquals(
            CheckRow(
                "Open release page",
                "This update can't replace the installed app. Uninstall Speecher, then install " +
                    "it from the release page.",
            ),
            checkRow(
                installFailure(update, PackageInstaller.STATUS_FAILURE_CONFLICT),
                UpdateChannel.Stable,
            ),
        )
        assertEquals(
            "Couldn't install the update.",
            installFailure(update, PackageInstaller.STATUS_FAILURE).message,
        )
    }

    @Test
    fun `release notes keep headings and bullets, joining hard-wrapped lines`() {
        assertEquals(
            listOf(
                NoteLine(NoteKind.Heading, "Added"),
                NoteLine(NoteKind.Bullet, "Check for updates from Settings, and read these notes."),
                NoteLine(NoteKind.Bullet, "A Nightly channel."),
                NoteLine(NoteKind.Text, "Install and update instructions: https://example.com"),
            ),
            noteLines(
                "## Added\r\n\r\n- Check for updates from Settings, and read\r\n  these notes.\r\n" +
                    "- A Nightly channel.\r\n\r\nInstall and update instructions: https://example.com"
            ),
        )
    }
}
