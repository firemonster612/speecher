package app.speecher.protocol

import java.nio.file.Files
import java.nio.file.Path
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

    val refined =
        refineTranscript(
            OkHttpClient(),
            OAuthProvider.ChatGpt,
            OAuthTokens(access, "", field("id_token"), 0, ""),
            "um so i think we should uh ship it on tuesday",
            emptyList(),
        )
    println("refinement: \"$refined\"")
}
