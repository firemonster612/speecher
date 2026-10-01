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
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.R
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SetupStatus
import app.speecher.android.dictation.label
import app.speecher.android.dictation.providerOrder

/**
 * First-run checklist: the four steps dictation needs, then Done. Each row reads one [SetupStatus]
 * flag, so the list updates as the app re-reads the system while it is open. Every other choice
 * lives in Settings.
 */
@Composable
fun Onboarding(
    status: SetupStatus,
    onSignIn: (Provider) -> Unit,
    onRequestMicrophone: () -> Unit,
    onOpenKeyboardSettings: () -> Unit,
    onOpenChipSettings: () -> Unit,
    onOpenAppInfo: () -> Unit,
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
            "Puts a dictation button on your keyboard. Speecher uses accessibility for this " +
                "alone, unless you turn on Screen text or Screenshot in Settings.",
            status.chipEnabled,
        ) {
            DictationButtonSteps(onOpenAppInfo, onOpenChipSettings)
        }
        Text(
            "Insert refined sends the text around your cursor to your provider, at its faster " +
                "tier. Change this in Settings.",
            Modifier.padding(start = 16.dp, end = 16.dp, top = 8.dp),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Button(
            onFinish,
            Modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, top = 24.dp),
            enabled = status.complete,
        ) {
            Text("Done")
        }
        if (!status.complete) {
            Text(
                "Finish the steps above",
                Modifier.fillMaxWidth().padding(16.dp),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
            )
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
    val title = "Sign in to one account"
    val done = signedIn.isNotEmpty()
    val remaining = providerOrder.filter { it !in signedIn }
    Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 14.dp)) {
        StepMarker(number, title, done)
        Column(Modifier.padding(start = 16.dp).weight(1f)) {
            Text(title, style = MaterialTheme.typography.titleMedium)
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
                    TextButton({ onSignIn(provider) }, Modifier.offset(x = (-12).dp)) {
                        Text("Add ${provider.label} for a choice of provider")
                    }
                }
            }
            if (signingIn != null && signingIn !in signedIn) PasteCode(signingIn, onPasteCode)
        }
    }
}

/**
 * Android blocks accessibility for sideloaded apps until the user allows restricted settings on the
 * app's info page, and that menu item only appears after Android has refused to turn the service on
 * once, so the steps start in Accessibility. They stay folded until asked for, since a Play Store
 * install never needs them. Success shows up through the chipEnabled poll; nothing here reads the
 * state.
 */
@Composable
private fun DictationButtonSteps(onOpenAppInfo: () -> Unit, onOpenChipSettings: () -> Unit) {
    var shown by rememberSaveable { mutableStateOf(false) }
    StepButton("Open accessibility settings", onOpenChipSettings)
    TextButton({ shown = !shown }, Modifier.offset(x = (-12).dp)) {
        Text(if (shown) "Hide steps" else "Android blocked it? Show steps")
    }
    if (!shown) return
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        listOf(
                "In Accessibility, find Speecher dictation button. Android marks it as restricted.",
                "Tap its greyed-out row. Android says it's blocked. Close that message.",
                "Open App info, tap the three-dot menu, then Allow restricted settings.",
                "Go back to Accessibility and turn Speecher dictation button on.",
            )
            .forEachIndexed { index, step ->
                Text("${'a' + index}. $step", style = MaterialTheme.typography.bodyMedium)
                when (index) {
                    1 -> BlockedRowIllustration()
                    2 -> {
                        AllowRestrictedIllustration()
                        StepButton("Open app info", onOpenAppInfo)
                    }
                }
            }
    }
}

/**
 * What a browser sign-in will look like, shown before the browser opens so the paste fallback is on
 * screen before anyone needs it.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SignInStepsSheet(provider: Provider, onOpen: () -> Unit, onDismiss: () -> Unit) {
    ModalBottomSheet(onDismiss, sheetState = rememberModalBottomSheetState(true)) {
        SignInSteps(provider, onOpen, onDismiss)
    }
}

@Composable
internal fun SignInSteps(provider: Provider, onOpen: () -> Unit, onCancel: () -> Unit) {
    Column(Modifier.verticalScroll(rememberScrollState()).padding(24.dp)) {
        Text("Sign in to ${provider.label}", style = MaterialTheme.typography.headlineSmall)
        Text(
            "A browser opens to ${provider.label}'s sign-in page and brings you back when you're " +
                "done. If it doesn't, long-press its address bar, copy the link (it starts with " +
                "http://localhost) and paste it in Speecher.",
            Modifier.padding(top = 16.dp),
            style = MaterialTheme.typography.bodyMedium,
        )
        // Shown before the button that asks for the notification permission.
        Text(
            "A \"Signing in\" notification keeps Speecher running while the browser is open.",
            Modifier.padding(top = 16.dp, bottom = 8.dp),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.End),
        ) {
            TextButton(onCancel) { Text("Cancel") }
            Button(onOpen) { Text("Open ${provider.label} sign-in") }
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
        StepMarker(number, title, done)
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
private fun StepMarker(number: Int, title: String, done: Boolean) {
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
                contentDescription = "$title done",
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

/** A field to try the dictation button on. Its text is thrown away. */
@Composable
internal fun PracticeField(modifier: Modifier = Modifier) {
    var text by rememberSaveable { mutableStateOf("") }
    OutlinedTextField(
        text,
        { text = it },
        modifier.fillMaxWidth(),
        placeholder = { Text("Tap here, then tap the dictation button") },
        minLines = 3,
    )
}

@Composable
internal fun ScreenTitle(title: String, subtitle: String? = null) {
    Column(Modifier.padding(start = 16.dp, end = 16.dp, top = 24.dp, bottom = 8.dp)) {
        Text(
            title,
            Modifier.semantics { heading() },
            style = MaterialTheme.typography.headlineMedium,
        )
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
        Modifier.padding(start = 16.dp, end = 16.dp, top = 24.dp, bottom = 8.dp).semantics {
            heading()
        },
        style = MaterialTheme.typography.titleSmall,
        color = MaterialTheme.colorScheme.primary,
    )
}

@Composable
private fun OnboardingPreview(status: SetupStatus) = SpeecherTheme {
    Surface { Onboarding(status, {}, {}, {}, {}, {}, {}) }
}

@PreviewLightDark
@Composable
internal fun SignInStepsPreview() = SpeecherTheme {
    Surface { SignInSteps(Provider.ChatGpt, {}, {}) }
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
