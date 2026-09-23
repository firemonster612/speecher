package app.speecher.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Button
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
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.R
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SetupStatus
import app.speecher.android.dictation.providerOrder

/**
 * First-run checklist. Each row reads one [SetupStatus] flag, so the list updates as the app
 * re-reads the system while it is open.
 */
@Composable
fun Onboarding(
    status: SetupStatus,
    onSignIn: (Provider) -> Unit,
    onRequestMicrophone: () -> Unit,
    onOpenKeyboardSettings: () -> Unit,
    onOpenChipSettings: () -> Unit,
    onFinish: () -> Unit,
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
        var step = 1
        SignInStep(step++, status.signedIn, signingIn, onSignIn, onPasteCode)
        signInError?.let {
            Text(it, Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.error)
        }
        Step(
            step++,
            "Allow the microphone",
            "The keyboard can't ask for it, so Speecher asks here.",
            status.microphoneGranted,
        ) {
            StepButton("Allow", onRequestMicrophone)
        }
        Step(
            step++,
            "Turn on the Speecher keyboard",
            "It only listens. You keep typing with your usual keyboard.",
            status.keyboardEnabled,
        ) {
            StepButton("Open settings", onOpenKeyboardSettings)
        }
        Step(
            step++,
            "Turn on the dictation button",
            "It shows a small button beside your keyboard and lets it switch to Speecher when you " +
                "tap. Android may ask you to allow this for a sideloaded app.",
            status.chipEnabled,
        ) {
            StepButton("Open settings", onOpenChipSettings)
        }
        Section("Try it")
        PracticeField(Modifier.padding(horizontal = 16.dp))
        Button(
            onFinish,
            Modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, top = 24.dp),
            enabled = status.complete,
        ) {
            Text("Done")
        }
    }
}

/**
 * Sign-in is one step, not one per provider: either account is enough to finish setup, so the step
 * is done the moment one connects and the other is offered as an optional extra.
 */
@Composable
private fun SignInStep(
    number: Int,
    signedIn: Set<Provider>,
    signingIn: Provider?,
    onSignIn: (Provider) -> Unit,
    onPasteCode: (String) -> Unit,
) {
    val done = signedIn.isNotEmpty()
    val remaining = providerOrder.filter { it !in signedIn }
    Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 14.dp)) {
        StepMarker(number, done)
        Column(Modifier.padding(start = 16.dp).weight(1f)) {
            Text("Sign in to one account", style = MaterialTheme.typography.titleMedium)
            Text(
                "Speecher transcribes with your own ChatGPT or Claude account. One is enough.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            if (!done) {
                Spacer(Modifier.height(10.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    remaining.forEach { provider ->
                        StepButton("Sign in to ${provider.label}") { onSignIn(provider) }
                    }
                }
            } else {
                remaining.forEach { provider ->
                    TextButton({ onSignIn(provider) }) {
                        Text("Add ${provider.label} for a choice of provider")
                    }
                }
            }
            if (signingIn != null) PasteCode(onPasteCode)
        }
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
    Surface { Onboarding(status, {}, {}, {}, {}, {}) }
}

@PreviewLightDark
@Composable
internal fun OnboardingFreshPreview() =
    OnboardingPreview(SetupStatus(emptySet(), false, false, false))

@PreviewLightDark
@Composable
internal fun OnboardingPartwayPreview() =
    OnboardingPreview(SetupStatus(setOf(Provider.Claude), true, true, false))

@PreviewLightDark
@Composable
internal fun OnboardingDonePreview() =
    OnboardingPreview(SetupStatus(Provider.entries.toSet(), true, true, true))
