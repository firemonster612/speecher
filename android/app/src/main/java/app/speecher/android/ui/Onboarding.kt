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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
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
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.R
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SetupStatus
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.android.dictation.label
import app.speecher.android.dictation.providerOrder

/**
 * First-run checklist. Each row reads one [SetupStatus] flag, so the list updates as the app
 * re-reads the system while it is open. The choices below the steps are optional and go out whole
 * through [onChangeSettings], as in Settings.
 */
@Composable
fun Onboarding(
    status: SetupStatus,
    settings: SpeecherSettings,
    onChangeSettings: (SpeecherSettings) -> Unit,
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
            "It shows a small button beside your keyboard and switches to Speecher when you tap it.",
            status.chipEnabled,
        ) {
            RestrictedSettingsSteps(onOpenAppInfo, onOpenChipSettings)
        }
        Section("Buttons")
        ButtonLayoutPicker(settings.buttonLayout) {
            onChangeSettings(settings.copy(buttonLayout = it))
        }
        Section("Dictation panel size")
        PanelSizePicker(settings.panelSize) { onChangeSettings(settings.copy(panelSize = it)) }
        Section("Refinement")
        OptionalSwitch(
            "Fast mode",
            FAST_MODE_DESCRIPTION,
            settings.chatGptFastMode && settings.claudeFastMode,
        ) {
            onChangeSettings(settings.copy(chatGptFastMode = it, claudeFastMode = it))
        }
        Section("Optional context")
        OptionalSwitch(
            "Screen text",
            "Lets refinement read the app you're dictating into.",
            settings.includeScreenText,
        ) {
            onChangeSettings(settings.copy(includeScreenText = it))
        }
        OptionalSwitch(
            "Screenshot",
            "Sends a picture of the screen to your refinement provider. Needs a vision model.",
            settings.includeScreenshot,
        ) {
            onChangeSettings(settings.copy(includeScreenshot = it))
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
            if (signingIn != null) PasteCode(signingIn, onPasteCode)
        }
    }
}

/**
 * Android blocks accessibility for sideloaded apps until the user allows restricted settings on the
 * app's info page, and that menu item only appears after Android has refused to turn the service on
 * once, so the steps start in Accessibility. Success shows up through the chipEnabled poll; nothing
 * here reads the state.
 */
@Composable
private fun RestrictedSettingsSteps(onOpenAppInfo: () -> Unit, onOpenChipSettings: () -> Unit) {
    Text(
        "Android blocks accessibility for apps installed outside the Play Store. Speecher needs " +
            "it only to show the dictation button over your keyboard and switch keyboards when " +
            "you tap it. It doesn't read your screen unless you turn on Screen text or Screenshot " +
            "below.",
        style = MaterialTheme.typography.bodyMedium,
    )
    Spacer(Modifier.height(10.dp))
    AcknowledgedSteps(
        listOf(
            "Open Accessibility settings.",
            "Find Speecher chip. It's marked as restricted.",
            "Tap the greyed-out Speecher chip row. Android says it's blocked. Close that message.",
            "Open App info, tap the three-dot menu, then Allow restricted settings.",
            "Go back to Accessibility and turn Speecher chip on.",
        ),
        stepDetail = { index, understood ->
            when (index) {
                0 ->
                    FilledTonalButton(onOpenChipSettings, enabled = understood) {
                        Text("Open accessibility settings")
                    }
                2 -> BlockedRowIllustration()
                3 -> {
                    AllowRestrictedIllustration()
                    FilledTonalButton(onOpenAppInfo, enabled = understood) { Text("Open app info") }
                }
            }
        },
    )
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
        Text("Before you sign in", style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(16.dp))
        AcknowledgedSteps(
            listOf(
                "A browser opens to ${provider.label}'s sign-in page.",
                "Sign in and approve Speecher.",
                "The browser should bring you back to Speecher on its own.",
                "If it doesn't, long-press the address bar and copy the link. It starts with " +
                    "http://localhost. Come back to Speecher and paste it.",
            )
        ) { understood ->
            // Shown before the button that asks for the notification permission.
            Text(
                "A \"Signing in\" notification keeps Speecher running while the browser is open.",
                Modifier.padding(bottom = 8.dp),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Row(
                Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.End),
            ) {
                TextButton(onCancel) { Text("Cancel") }
                Button(onOpen, enabled = understood) { Text("Open ${provider.label} sign-in") }
            }
        }
    }
}

/**
 * Numbered [steps] people tend to skip, then an "I understand" box that gates the [actions], so
 * nobody reaches them without the steps on screen. [stepDetail] adds content under a step, given
 * its index and whether the box is ticked, for actions that belong beside that step.
 */
@Composable
internal fun AcknowledgedSteps(
    steps: List<String>,
    stepDetail: @Composable (Int, Boolean) -> Unit = { _, _ -> },
    actions: @Composable (Boolean) -> Unit = {},
) {
    var understood by rememberSaveable { mutableStateOf(false) }
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        steps.forEachIndexed { index, step ->
            Text("${index + 1}. $step", style = MaterialTheme.typography.bodyMedium)
            stepDetail(index, understood)
        }
    }
    Row(
        Modifier.fillMaxWidth().padding(vertical = 8.dp).toggleable(
            understood,
            role = Role.Checkbox,
        ) {
            understood = it
        },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Checkbox(understood, onCheckedChange = null)
        Text("I understand", Modifier.padding(start = 12.dp))
    }
    actions(understood)
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
private fun OptionalSwitch(
    title: String,
    description: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
) {
    ListItem(
        headlineContent = { Text(title) },
        supportingContent = { Text(description) },
        trailingContent = { Switch(checked, onCheckedChange) },
        colors = ListItemDefaults.colors(containerColor = MaterialTheme.colorScheme.surface),
    )
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
    Surface { Onboarding(status, SpeecherSettings(), {}, {}, {}, {}, {}, {}, {}) }
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
