package app.speecher.android.ui

import android.content.res.Configuration
import android.graphics.Color.TRANSPARENT
import android.os.Bundle
import android.widget.ImageView
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import app.speecher.android.R

private val bottom: (@Composable () -> Unit) -> @Composable () -> Unit = { content ->
    { Box(Modifier.fillMaxSize(), contentAlignment = Alignment.BottomCenter) { content() } }
}

/**
 * Focuses a field so the real keyboard comes up, then docks the chip where the overlay puts it: 16
 * px left of the keyboard's right edge and 16 px above its top (m2-spike-findings, spike 1).
 */
@Composable
private fun ChipOverKeyboard() {
    val focus = remember { FocusRequester() }
    LaunchedEffect(Unit) { focus.requestFocus() }
    Box(Modifier.fillMaxSize().imePadding()) {
        Box(Modifier.statusBarsPadding().padding(16.dp)) {
            PracticeField(Modifier.focusRequester(focus))
        }
        DictationChip(
            {},
            {},
            { _, _ -> },
            Modifier.align(Alignment.BottomEnd).padding(with(LocalDensity.current) { 16.toDp() }),
        )
    }
}

/**
 * The adaptive launcher icon as the launcher masks it, and its monochrome layer as themed icons
 * tint it.
 */
@Composable
private fun LauncherIcons() {
    val tint = MaterialTheme.colorScheme.onSecondaryContainer.toArgb()
    val themedBackground = MaterialTheme.colorScheme.secondaryContainer
    Row(
        Modifier.fillMaxSize(),
        horizontalArrangement = Arrangement.SpaceEvenly,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        AndroidView(
            { ImageView(it).apply { setImageResource(R.mipmap.ic_launcher) } },
            Modifier.size(96.dp),
        )
        AndroidView(
            {
                ImageView(it).apply {
                    setImageResource(R.drawable.ic_launcher_foreground)
                    setColorFilter(tint)
                }
            },
            Modifier.size(144.dp).clip(CircleShape).background(themedBackground),
        )
    }
}

/** One entry per preview state, keyed by the `state` extra. */
private val states: Map<String, @Composable () -> Unit> =
    mapOf(
        "panel-connecting" to bottom { PanelConnectingPreview() },
        "panel-listening-empty" to bottom { PanelListeningEmptyPreview() },
        "panel-listening" to bottom { PanelListeningPreview() },
        "panel-listening-no-refine" to bottom { PanelListeningNoRefinePreview() },
        "panel-refining" to bottom { PanelRefiningPreview() },
        "panel-failed-microphone" to bottom { PanelFailedMicrophonePreview() },
        "panel-failed-signed-out" to bottom { PanelFailedSignedOutPreview() },
        "panel-failed-network" to bottom { PanelFailedNetworkPreview() },
        "panel-failed-provider" to bottom { PanelFailedProviderPreview() },
        "chip" to { ChipOverKeyboard() },
        "save-position-pill" to { SavePositionPillPreview() },
        "launcher-icon" to { LauncherIcons() },
        "home-ready" to { HomeReadyPreview() },
        "home-pending" to { HomeSetupPendingPreview() },
        "onboarding-fresh" to { OnboardingFreshPreview() },
        "onboarding-partway" to { OnboardingPartwayPreview() },
        "onboarding-done" to { OnboardingDonePreview() },
        "settings" to { SettingsPreview() },
        "settings-refinement-off" to { SettingsRefinementOffPreview() },
    )

/**
 * Debug builds only. Shows one preview state full screen so it can be screenshotted on a device:
 * `am start -n app.speecher.android/.ui.PreviewGalleryActivity --es state panel-listening --ez dark
 * true`.
 */
class PreviewGalleryActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        val name = intent.getStringExtra("state")
        val dark = intent.getBooleanExtra("dark", false)
        val bars =
            if (dark) SystemBarStyle.dark(TRANSPARENT)
            else SystemBarStyle.light(TRANSPARENT, TRANSPARENT)
        enableEdgeToEdge(bars, bars)
        super.onCreate(savedInstanceState)
        setContent {
            val night =
                if (dark) Configuration.UI_MODE_NIGHT_YES else Configuration.UI_MODE_NIGHT_NO
            val config =
                Configuration(LocalConfiguration.current).apply {
                    uiMode = (uiMode and Configuration.UI_MODE_NIGHT_MASK.inv()) or night
                }
            CompositionLocalProvider(LocalConfiguration provides config) {
                SpeecherTheme {
                    Box(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background)) {
                        states[name]?.invoke()
                            ?: Text(
                                states.keys.joinToString("\n"),
                                Modifier.statusBarsPadding().padding(16.dp),
                            )
                    }
                }
            }
        }
    }
}
