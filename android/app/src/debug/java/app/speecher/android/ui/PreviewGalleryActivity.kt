package app.speecher.android.ui

import android.content.res.Configuration
import android.graphics.Color.TRANSPARENT
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.unit.dp
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SetupStatus
import app.speecher.android.dictation.SpeecherSettings

private val bottom: (@Composable () -> Unit) -> @Composable () -> Unit = { content ->
    { Box(Modifier.fillMaxSize(), contentAlignment = Alignment.BottomCenter) { content() } }
}

private val pending = SetupStatus(setOf(Provider.Claude), true, true, false, false)

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
        "chip" to { ChipPreview() },
        "home-ready" to { HomeReadyPreview() },
        "home-pending" to { HomeSetupPendingPreview() },
        "onboarding-fresh" to
            {
                SpeecherScreen("", null) {
                    Onboarding(SetupStatus(emptySet(), false, false, false, false), {}, {}, {}, {})
                }
            },
        "onboarding-partway" to
            {
                SpeecherScreen("", null) { Onboarding(pending, {}, {}, {}, {}) }
            },
        "settings" to
            {
                SpeecherScreen("Settings", {}) {
                    Settings(
                        SpeecherSettings(
                            vocabulary = listOf("Speecher", "Kirigami", "Priya Raman")
                        ),
                        Provider.entries.toSet(),
                        {},
                        {},
                        {},
                    )
                }
            },
        "settings-refinement-off" to
            {
                SpeecherScreen("Settings", {}) {
                    Settings(
                        SpeecherSettings(Provider.ChatGpt, refinementEnabled = false),
                        setOf(Provider.ChatGpt),
                        {},
                        {},
                        {},
                    )
                }
            },
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
