package app.speecher.protocol

import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.PI
import kotlin.math.sin
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import okhttp3.OkHttpClient

/** Opt-in live check of chatgpt.com's transcribe and refinement endpoints over BC TLS HTTP. */
fun main() {
    val tokens =
        Json.parseToJsonElement(
                Files.readString(Path.of(System.getProperty("user.home"), ".codex/auth.json"))
            )
            .jsonObject
            .getValue("tokens")
            .jsonObject
    fun field(name: String) = tokens.getValue(name).jsonPrimitive.content
    val access = field("access_token")
    println("TLS profile: ${TlsFingerprints.active.name}")

    // One second of a 440 Hz tone: enough for the endpoint to decode, no speech expected.
    val pcm = ByteArray(32000)
    for (i in 0 until 16000) {
        val sample = (sin(2 * PI * 440 * i / 16000) * 8000).toInt()
        pcm[i * 2] = sample.toByte()
        pcm[i * 2 + 1] = (sample shr 8).toByte()
    }
    // transcribeSpeech throws unless the reply is HTTP 2xx with a JSON "text" field.
    val text = transcribeSpeech(access, pcm)
    println("transcribe: HTTP 2xx, text=\"$text\"")

    // Each model the Settings picker offers, and each effort on the default model (gpt-6-luna).
    val choices =
        listOf("gpt-6-luna", "gpt-6-sol", "gpt-5.6-luna").map { it to "none" } +
            listOf("low", "medium", "high").map { "gpt-6-luna" to it }
    val failures = choices.count { (model, effort) ->
        val result = runCatching {
            refineTranscript(
                OkHttpClient(),
                OAuthProvider.ChatGpt,
                OAuthTokens(access, "", field("id_token"), 0, ""),
                "um so i think we should uh ship it on tuesday",
                emptyList(),
                model,
                effort,
                RefinementContext(),
            )
        }
        println(
            "refinement $model effort=$effort: " +
                result.fold({ "\"$it\"" }, { "FAILED ${it.message}" })
        )
        result.isFailure
    }
    check(failures == 0) { "$failures refinement choices failed" }

    // Fast mode: service_tier=priority, falling back to standard speed if chatgpt.com refuses it.
    val fast = AtomicBoolean(true)
    val fastResult =
        refineTranscript(
            OkHttpClient(),
            OAuthProvider.ChatGpt,
            OAuthTokens(access, "", field("id_token"), 0, ""),
            "um so i think we should uh ship it on tuesday",
            emptyList(),
            "gpt-6-luna",
            "none",
            RefinementContext(),
            fastMode = fast,
        )
    println(
        "refinement fast mode: \"$fastResult\" " +
            if (fast.get()) "(service_tier=priority accepted)"
            else "(service_tier=priority rejected, standard-speed fallback answered)"
    )

    // A populated target context: Messages, casual tone, text around a selected word.
    val context =
        resolveRefinementContext(
            "com.google.android.apps.messaging",
            "Messages",
            null,
            NearbyText("Dinner at ", " works for me", 10, 15),
            WritingProfile.Other,
            mapOf(WritingProfile.Other to WritingProfileSettings(tone = Tone.Casual)),
        )
    println("context: ${context.category.id}/${context.profile.id}/${context.tone.id}")
    val partials = mutableListOf<String>()
    val casual =
        refineTranscript(
            OkHttpClient(),
            OAuthProvider.ChatGpt,
            OAuthTokens(access, "", field("id_token"), 0, ""),
            "um so are we still on for seven or should we push it to eight",
            emptyList(),
            "gpt-6-luna",
            "none",
            context,
            onText = partials::add,
        )
    println("refinement with context: \"$casual\"")
    // Each partial extends the last, and the final result is the last partial.
    check(partials.zipWithNext().all { (a, b) -> b.startsWith(a) } && partials.last() == casual)
    println("streamed ${partials.size} partials: ${partials.joinToString(" | ") { "\"$it\"" }}")

    // The a11y capture's keys: the chat's window title and its visible messages.
    val screen =
        context.copy(
            controlRole = "short message",
            fieldHint = "Message",
            windowTitle = "Priya Raman",
            screenText = "Priya Raman\nAre we still doing Kowloon Kitchen tonight?\nMessage",
        )
    val withScreen =
        refineTranscript(
            OkHttpClient(),
            OAuthProvider.ChatGpt,
            OAuthTokens(access, "", field("id_token"), 0, ""),
            "yeah tell pre are we're still on for cow loon kitchen at seven",
            emptyList(),
            "gpt-6-luna",
            "none",
            screen,
        )
    println("refinement with window title and screen text: \"$withScreen\"")
}
