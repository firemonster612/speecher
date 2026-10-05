package app.speecher.android.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuAnchorType
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import app.speecher.android.auth.ApiKey
import app.speecher.android.auth.TokenStore
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.android.dictation.sharedHttp
import app.speecher.protocol.EndpointCheck
import app.speecher.protocol.checkSpeechEndpoint
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * The speech Custom Endpoint's server, path, key and model, and a test of its connection that lists
 * the server's models. A test that finds models while none is set picks the first, as on the
 * desktop.
 */
@Composable
internal fun SpeechEndpointSettings(
    settings: SpeecherSettings,
    onChange: (SpeecherSettings) -> Unit,
) {
    val endpoint = settings.speechEndpoint
    val latest by rememberUpdatedState(settings)
    val context = LocalContext.current
    val store = remember(context) { TokenStore(context) }
    var key by remember { mutableStateOf(store.apiKey(ApiKey.SpeechEndpoint)) }
    // The verdict on this server and key; changing either makes it stale.
    var check by remember(endpoint.server, key) { mutableStateOf<EndpointCheck?>(null) }
    var checking by remember(endpoint.server, key) { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    Section("Custom Endpoint")
    EndpointField(
        "Server URL",
        "Any server with an OpenAI-style audio transcriptions API.",
        endpoint.baseUrl,
        KeyboardType.Uri,
    ) {
        onChange(settings.copy(speechEndpoint = endpoint.copy(baseUrl = it)))
    }
    EndpointField("Path", "whisper.cpp uses /inference.", endpoint.path, KeyboardType.Uri) {
        onChange(settings.copy(speechEndpoint = endpoint.copy(path = it)))
    }
    ApiKeyField(ApiKey.SpeechEndpoint, key) { key = it }
    ModelField(endpoint.model, check?.models.orEmpty()) {
        onChange(settings.copy(speechEndpoint = endpoint.copy(model = it)))
    }
    ConnectionTest(if (checking) "Checking…" else check?.message ?: "Not tested yet.") {
        checking = true
        scope.launch {
            val result =
                withContext(Dispatchers.IO) { checkSpeechEndpoint(sharedHttp, endpoint, key) }
            check = result
            checking = false
            val now = latest.speechEndpoint
            if (now.server == endpoint.server && now.model.isEmpty() && result.models.isNotEmpty())
                onChange(latest.copy(speechEndpoint = now.copy(model = result.models.first())))
        }
    }
}

/** A one-line field under its [title] and what it is for. */
@Composable
private fun EndpointField(
    title: String,
    description: String,
    value: String,
    keyboard: KeyboardType,
    secret: Boolean = false,
    onValueChange: (String) -> Unit,
) {
    Column(Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
        FieldHeading(title, description)
        OutlinedTextField(
            value,
            onValueChange,
            Modifier.fillMaxWidth(),
            singleLine = true,
            visualTransformation =
                if (secret) PasswordVisualTransformation() else VisualTransformation.None,
            keyboardOptions = KeyboardOptions(autoCorrectEnabled = false, keyboardType = keyboard),
        )
    }
}

@Composable
private fun FieldHeading(title: String, description: String) {
    Text(title, style = MaterialTheme.typography.bodyLarge)
    Text(
        description,
        style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

/**
 * An optional key for a server or an API, saved encrypted in [slot] as it is typed, or cleared when
 * emptied. A key that could not be saved says so.
 */
@Composable
internal fun ApiKeyField(slot: ApiKey, key: String, onKeyChange: (String) -> Unit) {
    val context = LocalContext.current
    val store = remember(context) { TokenStore(context) }
    var saveFailed by remember { mutableStateOf(false) }
    EndpointField(
        "API key",
        "Optional. Stored encrypted on this phone.",
        key,
        KeyboardType.Password,
        secret = true,
    ) {
        saveFailed = runCatching { store.saveApiKey(slot, it) }.isFailure
        onKeyChange(it)
    }
    if (saveFailed) {
        Text(
            "Couldn't save the key. Try again.",
            Modifier.padding(horizontal = 16.dp),
            color = MaterialTheme.colorScheme.error,
        )
    }
}

/** The model to ask for: typed, or picked from the [models] the last connection test listed. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun ModelField(model: String, models: List<String>, onModelChange: (String) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    Column(Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
        FieldHeading("Model", "Test the connection to list the server's models, or type one.")
        ExposedDropdownMenuBox(expanded && models.isNotEmpty(), { expanded = it }) {
            OutlinedTextField(
                model,
                onModelChange,
                Modifier.fillMaxWidth()
                    .menuAnchor(ExposedDropdownMenuAnchorType.PrimaryEditable, models.isNotEmpty()),
                singleLine = true,
                trailingIcon =
                    if (models.isEmpty()) null
                    else {
                        { ExposedDropdownMenuDefaults.TrailingIcon(expanded) }
                    },
                keyboardOptions = KeyboardOptions(autoCorrectEnabled = false),
            )
            ExposedDropdownMenu(expanded && models.isNotEmpty(), { expanded = false }) {
                models.forEach {
                    DropdownMenuItem(
                        { Text(it) },
                        onClick = {
                            onModelChange(it)
                            expanded = false
                        },
                    )
                }
            }
        }
    }
}

/** The last connection test's [verdict] and the button that runs one. */
@Composable
internal fun ConnectionTest(verdict: String, onTest: () -> Unit) {
    ListItem(
        headlineContent = { Text("Connection") },
        supportingContent = { Text(verdict) },
        trailingContent = { FilledTonalButton(onTest) { Text("Test connection") } },
        colors = rowColors(),
    )
}
