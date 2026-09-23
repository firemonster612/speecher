package app.speecher.android.ui

import android.content.ClipData
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.ClipEntry
import androidx.compose.ui.platform.LocalClipboard
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.R
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SetupStatus
import kotlinx.coroutines.launch

/** The one-time grant that lets the chip swap keyboards without a system prompt. */
const val SwapGrantCommand =
    "adb shell pm grant app.speecher.android android.permission.WRITE_SECURE_SETTINGS"

/**
 * First-run checklist. Each row reads one [SetupStatus] flag, so the list updates as the engine
 * re-reads the system (for example, the adb grant turns done while the user is at a computer).
 */
@Composable
fun Onboarding(
    status: SetupStatus,
    onSignIn: (Provider) -> Unit,
    onRequestMicrophone: () -> Unit,
    onOpenKeyboardSettings: () -> Unit,
    onOpenChipSettings: () -> Unit,
    modifier: Modifier = Modifier,
    signingIn: Provider? = null,
    signInError: String? = null,
    onPasteCode: (String) -> Unit = {},
) {
    Column(modifier) {
        ScreenTitle(
            "Set up Speecher",
            "A few one-time steps so you can dictate into any text field.",
        )
        Provider.entries.forEachIndexed { index, provider ->
            Step(
                index + 1,
                "Sign in to ${provider.label}",
                "Speecher transcribes with your own account.",
                done = provider in status.signedIn,
            ) {
                StepButton("Sign in") { onSignIn(provider) }
            }
            if (signingIn == provider) PasteCode(onPasteCode)
        }
        signInError?.let {
            Text(it, Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.error)
        }
        Step(
            3,
            "Allow the microphone",
            "The keyboard can't ask for it, so Speecher asks here.",
            status.microphoneGranted,
        ) {
            StepButton("Allow", onRequestMicrophone)
        }
        Step(
            4,
            "Turn on the Speecher keyboard",
            "It only listens. You keep typing with your usual keyboard.",
            status.keyboardEnabled,
        ) {
            StepButton("Open settings", onOpenKeyboardSettings)
        }
        Step(
            5,
            "Turn on the dictation button",
            "Shows a small button beside your keyboard.",
            status.chipEnabled,
        ) {
            StepButton("Open settings", onOpenChipSettings)
        }
        Step(
            6,
            "Grant keyboard switching",
            "Run this once from a computer with USB debugging on.",
            status.swapGranted,
        ) {
            GrantCommand()
        }
        Section("Try it")
        PracticeField(Modifier.padding(horizontal = 16.dp))
    }
}

@Composable
private fun Step(
    number: Int,
    title: String,
    description: String,
    done: Boolean,
    action: @Composable () -> Unit,
) {
    Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 14.dp)) {
        StepMarker(number, done)
        Column(Modifier.padding(start = 16.dp).weight(1f)) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            Text(
                description,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            if (!done) {
                Spacer(Modifier.height(10.dp))
                action()
            }
        }
    }
}

@Composable
private fun StepMarker(number: Int, done: Boolean) {
    val colors = MaterialTheme.colorScheme
    val shape = Modifier.size(28.dp)
    Box(
        if (done) shape.background(colors.primary, CircleShape)
        else shape.border(1.5.dp, colors.outline, CircleShape),
        contentAlignment = Alignment.Center,
    ) {
        if (done) {
            Icon(
                painterResource(R.drawable.ic_check),
                contentDescription = "Done",
                Modifier.size(18.dp),
                tint = colors.onPrimary,
            )
        } else {
            Text(
                "$number",
                style = MaterialTheme.typography.labelLarge,
                color = colors.onSurfaceVariant,
            )
        }
    }
}

@Composable
private fun StepButton(text: String, onClick: () -> Unit) {
    FilledTonalButton(onClick) { Text(text) }
}

@Composable
private fun GrantCommand() {
    val clipboard = LocalClipboard.current
    val scope = rememberCoroutineScope()
    Surface(
        shape = RoundedCornerShape(12.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant),
    ) {
        // Full width, so the long permission name fits on one line.
        Text(
            SwapGrantCommand,
            Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 10.dp),
            style = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
        )
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
        CircularProgressIndicator(Modifier.size(14.dp), strokeWidth = 2.dp)
        Text(
            "Waiting for the grant",
            Modifier.padding(start = 8.dp).weight(1f),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        TextButton({
            scope.launch {
                clipboard.setClipEntry(
                    ClipEntry(ClipData.newPlainText("adb command", SwapGrantCommand))
                )
            }
        }) {
            Icon(
                painterResource(R.drawable.ic_copy),
                contentDescription = null,
                Modifier.size(18.dp),
            )
            Text("Copy", Modifier.padding(start = 8.dp))
        }
    }
}

/** A field to try the chip on. Its text is thrown away. */
@Composable
internal fun PracticeField(modifier: Modifier = Modifier) {
    var text by rememberSaveable { mutableStateOf("") }
    OutlinedTextField(
        text,
        { text = it },
        modifier.fillMaxWidth(),
        placeholder = { Text("Tap here, then tap the button above the keyboard") },
        minLines = 3,
    )
}

@Composable
internal fun ScreenTitle(title: String, subtitle: String? = null) {
    Column(Modifier.padding(start = 16.dp, end = 16.dp, top = 24.dp, bottom = 8.dp)) {
        Text(title, style = MaterialTheme.typography.headlineMedium)
        if (subtitle != null) {
            Text(
                subtitle,
                Modifier.padding(top = 8.dp),
                style = MaterialTheme.typography.bodyLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
internal fun Section(title: String) {
    Text(
        title,
        Modifier.padding(start = 16.dp, end = 16.dp, top = 24.dp, bottom = 8.dp),
        style = MaterialTheme.typography.titleSmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

@Composable
private fun OnboardingPreview(status: SetupStatus) = SpeecherTheme {
    Surface { Onboarding(status, {}, {}, {}, {}) }
}

@PreviewLightDark
@Composable
internal fun OnboardingFreshPreview() =
    OnboardingPreview(SetupStatus(emptySet(), false, false, false, false))

@PreviewLightDark
@Composable
internal fun OnboardingPartwayPreview() =
    OnboardingPreview(SetupStatus(setOf(Provider.Claude), true, true, false, false))

@PreviewLightDark
@Composable
internal fun OnboardingDonePreview() =
    OnboardingPreview(SetupStatus(Provider.entries.toSet(), true, true, true, true))
