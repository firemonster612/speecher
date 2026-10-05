package app.speecher.protocol

import java.io.BufferedReader
import java.io.ByteArrayOutputStream
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import okhttp3.Headers.Companion.toHeaders
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.MultipartBody
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody

/**
 * The speech Custom Endpoint: an OpenAI-style audio transcriptions server the person runs or chose.
 * Fields are as typed; requests normalize them. Its API key is kept apart, encrypted.
 */
data class SpeechEndpoint(
    /** The server's origin, without the path. */
    val baseUrl: String = "",
    /** whisper.cpp's server answers on /inference. */
    val path: String = DEFAULT_SPEECH_ENDPOINT_PATH,
    val model: String = "",
) {
    /** The server's origin, or empty when none is set. */
    val server: String
        get() = endpointServerBase(baseUrl)

    val transcriptionsUrl: String
        get() {
            val path = path.trim().ifEmpty { DEFAULT_SPEECH_ENDPOINT_PATH }
            return server + if (path.startsWith('/')) path else "/$path"
        }
}

const val DEFAULT_SPEECH_ENDPOINT_PATH = "/v1/audio/transcriptions"

/** The server's origin from a URL as typed: trailing slashes and /v1 segments removed. */
fun endpointServerBase(url: String): String {
    var base = url.trim().trimEnd('/')
    while (base.endsWith("/v1")) base = base.removeSuffix("/v1").trimEnd('/')
    return base
}

/**
 * The spoken languages a Custom Endpoint is offered, as codes, Automatic first. Its server is
 * unknown; most speak Whisper's languages.
 */
val endpointSpeechLanguages: List<String> =
    listOf(AUTOMATIC_LANGUAGE) +
        ("af am ar as az ba be bg bn bo br bs ca cs cy da de el en es et eu fa fi fo fr gl gu " +
                "haw ha he hi hr ht hu hy id is it ja jw ka kk km kn ko la lb ln lo lt lv mg mi mk " +
                "ml mn mr ms mt my ne nl nn no oc pa pl ps pt ro ru sa sd si sk sl sn so sq sr su " +
                "sv sw ta te tg th tk tl tr tt uk ur uz vi yi yo yue zh")
            .split(' ')

/**
 * Transcription through the speech Custom Endpoint: the whole recording, as a WAV file, POSTed to
 * the server once [stop] is called. A server that streams answers with server-sent events, shown as
 * they arrive. The audio is sent once, so a failure ends the dictation, keeping any text the stream
 * already produced. [prompt] goes in the OpenAI `prompt` field, left out when empty, and [language]
 * in `language`, left out for Automatic.
 *
 * A batch server sends nothing while it transcribes, so [inactivityMillis] allows a long recording
 * on a slow machine; [deadlineMillis] caps a server that keeps trickling.
 */
class EndpointSpeechClient(
    private val http: OkHttpClient,
    private val endpoint: SpeechEndpoint,
    private val apiKey: String,
    private val prompt: String,
    private val language: String,
    private val events: (SpeechEvent) -> Unit,
    private val inactivityMillis: Long = 60_000,
    private val deadlineMillis: Long = 300_000,
) : SpeechClient {
    private val audio = ByteArrayOutputStream()
    private val cancellation = Cancellation()
    private val streamed = StringBuilder()
    private var stopped = false
    @Volatile private var cancelled = false

    override fun sendAudio(pcm: ByteArray) {
        synchronized(audio) { if (!stopped) audio.write(pcm) }
    }

    override fun stop() {
        val pcm =
            synchronized(audio) {
                if (stopped) return
                stopped = true
                audio.toByteArray()
            }
        thread(isDaemon = true, name = "speech-endpoint") { transcribe(pcm) }
    }

    override fun cancel() {
        cancelled = true
        cancellation.cancel()
    }

    private fun transcribe(pcm: ByteArray) {
        if (pcm.isEmpty()) {
            emit(SpeechEvent.Completed)
            return
        }
        try {
            val text = upload(pcm).trim()
            if (text.isNotEmpty()) emit(SpeechEvent.Final(text))
            emit(SpeechEvent.Completed)
        } catch (error: Exception) {
            // Text the stream already produced is the dictation, cut short; the audio is not sent
            // again, so keep what arrived.
            val heard = streamed.trim()
            if (heard.isNotEmpty()) emit(SpeechEvent.Final(heard.toString()))
            emit(SpeechEvent.Failed(failureKind(error), error.message.orEmpty()))
        }
    }

    private fun emit(event: SpeechEvent) {
        if (!cancelled) events(event)
    }

    private fun upload(pcm: ByteArray): String {
        val form =
            MultipartBody.Builder()
                .setType(MultipartBody.FORM)
                .addFormDataPart(
                    "file",
                    "dictation.wav",
                    wavFromPcm16Mono(pcm, 16000).toRequestBody("audio/wav".toMediaType()),
                )
        val model = endpoint.model.trim()
        if (model.isNotEmpty()) form.addFormDataPart("model", model)
        form.addFormDataPart("response_format", "json")
        if (language != AUTOMATIC_LANGUAGE) form.addFormDataPart("language", language)
        if (prompt.isNotEmpty()) form.addFormDataPart("prompt", prompt)
        // Servers that stream answer with text/event-stream; the rest ignore it.
        form.addFormDataPart("stream", "true")
        val call =
            serverHttp(http)
                .newBuilder()
                .readTimeout(inactivityMillis, TimeUnit.MILLISECONDS)
                .build()
                .newCall(
                    Request.Builder()
                        .url(endpoint.transcriptionsUrl)
                        .headers(bearer(apiKey).toHeaders())
                        .post(form.build())
                        .build()
                )
        val deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(deadlineMillis)
        return cancellation.request(call::cancel, deadline) {
            call.execute().use { response ->
                if (!response.isSuccessful)
                    throw ProviderFailure(
                        selfHostedFailureKind(response.code),
                        "Speech endpoint failed with HTTP ${response.code}",
                        response.code,
                    )
                if (response.header("Content-Type").orEmpty().startsWith("text/event-stream"))
                    readStream(response.body.charStream().buffered())
                else jsonObject(response.body.string())?.text("text").orEmpty()
            }
        }
    }

    /**
     * `transcript.text.delta` events carry pieces, `transcript.text.done` the whole. Speaches
     * before 0.9 sends untyped `{"text": …}` events, one per segment.
     */
    private fun readStream(reader: BufferedReader): String {
        var done = ""
        forEachServerSentEvent(reader) { name, data ->
            val event = jsonObject(data) ?: JsonObject(emptyMap())
            val type = event.string("type") ?: name
            if (type == "error" || name == "error")
                throw ProviderFailure(
                    streamedErrorKind(event["error"] as? JsonObject ?: JsonObject(emptyMap())),
                    "Speech endpoint streamed an error",
                )
            val piece =
                when (type) {
                    "transcript.text.delta" -> event.text("delta")
                    "" -> spacedSegment(streamed, event.text("text").trim())
                    "transcript.text.done" -> {
                        done = event.text("text")
                        ""
                    }
                    else -> ""
                }
            if (piece.isNotEmpty()) {
                streamed.append(piece)
                emit(SpeechEvent.Partial(streamed.toString()))
            }
        }
        return done.ifEmpty { streamed.toString() }
    }
}

/** Calls [onEvent] with each complete event's name and data; an unfinished last one is dropped. */
private fun forEachServerSentEvent(reader: BufferedReader, onEvent: (String, String) -> Unit) {
    var name = ""
    val data = StringBuilder()
    while (true) {
        val line = reader.readLine() ?: return
        when {
            line.isEmpty() -> {
                onEvent(name, data.toString())
                name = ""
                data.clear()
            }
            line.startsWith("event:") -> name = line.substringAfter(':').trim()
            line.startsWith("data:") -> data.append(line.substringAfter(':').trim())
        }
    }
}

/**
 * A trimmed segment as it follows [text]: after a space, the way Whisper spaces segments, except
 * next to a script written without spaces between words.
 */
private fun spacedSegment(text: CharSequence, segment: String): String =
    if (
        text.isEmpty() ||
            segment.isEmpty() ||
            firstLetterWrittenWithoutSpaces(text.codePoints().toArray().reversed()) ||
            firstLetterWrittenWithoutSpaces(segment.codePoints().toArray().asList())
    )
        segment
    else " $segment"

/**
 * Whether the first character with a script of its own, past punctuation and digits, is in a script
 * written without spaces between words.
 */
private fun firstLetterWrittenWithoutSpaces(codePoints: List<Int>): Boolean =
    codePoints
        .map(Character.UnicodeScript::of)
        .firstOrNull { it !in sharedScripts }
        ?.let { it in scriptsWithoutSpaces } == true

private val sharedScripts =
    setOf(
        Character.UnicodeScript.COMMON,
        Character.UnicodeScript.INHERITED,
        Character.UnicodeScript.UNKNOWN,
    )

private val scriptsWithoutSpaces =
    setOf(
        Character.UnicodeScript.HAN,
        Character.UnicodeScript.HIRAGANA,
        Character.UnicodeScript.KATAKANA,
        Character.UnicodeScript.THAI,
        Character.UnicodeScript.LAO,
        Character.UnicodeScript.KHMER,
        Character.UnicodeScript.MYANMAR,
        Character.UnicodeScript.TIBETAN,
    )

/** The answer to Test connection. */
data class EndpointCheck(
    val ok: Boolean,
    /** One line a person can act on. */
    val message: String,
    /** The server's models, for the model picker; empty when it lists none. */
    val models: List<String> = emptyList(),
)

/** Tests the speech Custom Endpoint by its model listing. Blocking, a few seconds at most. */
fun checkSpeechEndpoint(
    http: OkHttpClient,
    endpoint: SpeechEndpoint,
    apiKey: String,
): EndpointCheck {
    val server = endpoint.server
    if (server.isEmpty()) return EndpointCheck(false, "Enter the server URL.")
    return checkModelListing(http, "$server/v1/models", bearer(apiKey), server)
}

/**
 * Asks for the model listing at [url] with [headers] and says what the answer means for [server]:
 * unreachable, a refused key, or connected, with the models it lists. Blocking, five seconds at
 * most.
 */
fun checkModelListing(
    http: OkHttpClient,
    url: String,
    headers: Map<String, String>,
    server: String,
): EndpointCheck {
    val (status, body) =
        try {
            serverHttp(http)
                .newBuilder()
                .callTimeout(5, TimeUnit.SECONDS)
                .build()
                .newCall(Request.Builder().url(url).headers(headers.toHeaders()).build())
                .execute()
                .use { it.code to it.body.string() }
        } catch (error: Exception) {
            return EndpointCheck(false, "Could not reach $server: ${error.message}")
        }
    if (status == 401 || status == 403)
        return EndpointCheck(false, "The server refused the API key (HTTP $status).")
    val models = if (status == 200) modelIdsFromListing(body) else emptyList()
    if (models.isEmpty())
        return EndpointCheck(true, "Connected. The server does not list its models; type one.")
    return EndpointCheck(true, "Connected. Models available: ${models.size}.", models)
}

/**
 * Model ids from an OpenAI-style `{"data":[{"id":..}]}` or Ollama-style `{"models":[{"name":..}]}`
 * listing.
 */
fun modelIdsFromListing(body: String): List<String> {
    val listing = jsonObject(body) ?: return emptyList()
    fun ids(key: String, field: String) =
        (listing[key] as? JsonArray).orEmpty().map { (it as? JsonObject)?.text(field).orEmpty() }
    return (ids("data", "id") + ids("models", "name")).filter(String::isNotEmpty)
}

/** The Authorization header for an optional API key. */
fun bearer(apiKey: String): Map<String, String> =
    if (apiKey.isEmpty()) emptyMap() else mapOf("Authorization" to "Bearer $apiKey")

/**
 * Requests to a server the person chose stay on it: a redirect is answered, not followed, so
 * neither the key nor the audio goes anywhere else.
 */
private fun serverHttp(http: OkHttpClient): OkHttpClient =
    http.newBuilder().followRedirects(false).build()

private fun jsonObject(text: String): JsonObject? = runCatching {
    Json.parseToJsonElement(text) as? JsonObject
}
    .getOrNull()

private fun JsonObject.string(key: String): String? =
    (this[key] as? JsonPrimitive)?.takeIf { it.isString }?.content

private fun JsonObject.text(key: String): String = string(key).orEmpty()
