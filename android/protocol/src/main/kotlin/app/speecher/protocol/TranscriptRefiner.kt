package app.speecher.protocol

import java.io.BufferedReader
import java.net.SocketTimeoutException
import java.util.Base64
import java.util.UUID
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonArray
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.jsonPrimitive
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody

fun refineTranscript(
    http: OkHttpClient,
    provider: OAuthProvider,
    tokens: OAuthTokens,
    rawTranscript: String,
    vocabulary: List<VocabularyWord>,
    /** The API model id, and the effort sent as `reasoning.effort` or `output_config.effort`. */
    model: String,
    effort: String,
    context: RefinementContext,
    endpointBase: String =
        if (provider == OAuthProvider.Claude) "https://api.anthropic.com/v1"
        else "https://chatgpt.com/backend-api/codex",
    /**
     * Non-null asks for fast mode while it holds true. A fast request the provider rejects is
     * retried at standard speed, and when that succeeds this is set false so later requests skip
     * straight to standard speed.
     */
    fastMode: AtomicBoolean? = null,
    /** ChatGPT only: fast mode asks for the ultrafast tier, on a model that has one. */
    ultrafast: Boolean = false,
    /** Receives the refined text so far each time the stream adds to it. */
    onText: (String) -> Unit = {},
    /** Aborts the request in flight and every retry after it. */
    cancellation: Cancellation = Cancellation(),
    /**
     * The longest the stream may go without a byte, and the longest the whole refinement may take,
     * its fast-mode and screenshot retries included: the desktop's 20 s and 120 s.
     */
    inactivityMillis: Int = 20_000,
    deadlineMillis: Long = 120_000,
): String {
    var streamed = false
    val ultrafastTier = ultrafast && modelSupportsUltrafast(model)
    val timing =
        Timing(cancellation, inactivityMillis, System.nanoTime() + deadlineMillis * 1_000_000)
    fun refine(sent: RefinementContext, fast: Boolean) =
        refineOnce(
            http,
            provider,
            tokens,
            rawTranscript,
            vocabulary,
            model,
            effort,
            sent,
            fast,
            ultrafastTier,
            endpointBase,
            timing,
        ) {
            streamed = true
            onText(it)
        }
    // Fast mode must never cost the user their insert: anything that fails it before text streams
    // gets one standard-speed try with the same content, as the desktop's StreamingRefinement does.
    fun refineFastFirst(): String {
        if (
            fastMode == null ||
                !fastMode.get() ||
                provider == OAuthProvider.Claude && !modelSupportsFastMode(model)
        )
            return refine(context, fast = false)
        return try {
            refine(context, fast = true)
        } catch (failure: Exception) {
            if (streamed || failure is RefinementStopped) throw failure
            // A stall says nothing about fast mode, so like the desktop only a refusal latches it
            // off. BC TLS rethrows a socket read timeout as it is, so the chatgpt.com path stalls
            // with the same SocketTimeoutException as OkHttp's.
            refine(context, fast = false).also {
                if (failure !is SocketTimeoutException) fastMode.set(false)
            }
        }
    }
    if (context.screenshotJpeg == null) return refineFastFirst()
    // Only once standard speed has also refused the request is the image the suspect: a model
    // without vision rejects it, and an oversized one is refused, yet the dictation still deserves
    // a text-only pass. Other failures would fail again, so they are not retried. Both statuses
    // arrive before any text, so the retry never replays streamed output.
    return try {
        refineFastFirst()
    } catch (failure: ProviderFailure) {
        if (failure.httpStatus !in IMAGE_REJECTED_STATUSES) throw failure
        refine(context.copy(screenshotJpeg = null), fast = false)
    }
}

/**
 * Anthropic's fast mode is a research preview limited to Opus 5.5, Opus 5 and Opus 4.8 ("opus-5"
 * also matches "opus-5-5"); other models fail every request that asks for it.
 */
fun modelSupportsFastMode(model: String): Boolean =
    model.lowercase().let { it.contains("opus-5") || it.contains("opus-4-8") }

/** Anthropic models that take adaptive thinking and an effort; the rest are sent neither. */
private fun modelSupportsAdaptiveEffort(model: String): Boolean =
    model.lowercase().let { id ->
        listOf(
                "opus-5",
                "sonnet-5",
                "haiku-5",
                "sonnet-4-6",
                "opus-4-8",
                "opus-4-7",
                "opus-4-6",
                "opus-4-5",
            )
            .any { it in id }
    }

private fun modelSupportsExtraHighEffort(model: String): Boolean =
    model.lowercase().let { id ->
        listOf("opus-5", "sonnet-5", "haiku-5", "opus-4-8", "opus-4-7").any { it in id }
    }

/** The effort Anthropic is sent: max for xhigh on a model without it, high for an unknown one. */
private fun anthropicEffort(model: String, effort: String): String =
    when {
        effort == "xhigh" && !modelSupportsExtraHighEffort(model) -> "max"
        effort in setOf("low", "medium", "high", "xhigh", "max") -> effort
        else -> "high"
    }

/** Ultrafast serves only GPT-6 Astra so far. GPT-6.1 Sol is due to join it. */
fun modelSupportsUltrafast(model: String): Boolean =
    model.trim().lowercase().startsWith("gpt-6-astra")

/** A refinement request the provider answered with a non-2xx [status]. */
private fun refinementHttpFailure(status: Int) =
    ProviderFailure(failureKindForHttpStatus(status), "Refinement failed with HTTP $status", status)

/**
 * The provider ended the response itself (failed, incomplete, or an unexpected stop reason). The
 * desktop retries those at neither speed, so neither does the fast-mode fallback.
 */
private class RefinementStopped(kind: ProviderFailureKind, message: String) :
    ProviderFailure(kind, message)

/** What every request of one refinement shares: its cancellation, read timeout and deadline. */
private class Timing(
    val cancellation: Cancellation,
    val inactivityMillis: Int,
    val deadlineNanos: Long,
)

/** Bad request and payload too large: what a provider answers when it will not take the image. */
private val IMAGE_REJECTED_STATUSES = setOf(400, 413)

private fun refineOnce(
    http: OkHttpClient,
    provider: OAuthProvider,
    tokens: OAuthTokens,
    rawTranscript: String,
    vocabulary: List<VocabularyWord>,
    model: String,
    effort: String,
    context: RefinementContext,
    fast: Boolean,
    ultrafast: Boolean,
    endpointBase: String,
    timing: Timing,
    onText: (String) -> Unit,
): String {
    val base = endpointBase.trimEnd('/')
    if (provider == OAuthProvider.Claude) {
        val call =
            http
                .newBuilder()
                .readTimeout(timing.inactivityMillis.toLong(), TimeUnit.MILLISECONDS)
                .build()
                .newCall(
                    claudeRequest(
                        tokens.accessToken,
                        rawTranscript,
                        vocabulary,
                        model,
                        effort,
                        context,
                        fast,
                        base,
                    )
                )
        return timing.cancellation.request(call::cancel, timing.deadlineNanos) {
            call.execute().use { response ->
                if (!response.isSuccessful) throw refinementHttpFailure(response.code)
                readRefinement(provider, response.body.charStream().buffered(), onText)
            }
        }
    }
    // chatgpt.com sits behind Cloudflare, which rejects OkHttp's Conscrypt handshake with a 403.
    return httpPostStreaming(
        "$base/responses",
        listOfNotNull(
                "Authorization" to "Bearer ${tokens.accessToken}",
                tokens.accountId()?.let { "ChatGPT-Account-ID" to it },
            )
            .toMap(),
        HttpBody(
            "application/json",
            chatGptBody(
                    rawTranscript,
                    vocabulary,
                    model,
                    effort,
                    context,
                    if (!fast) null else if (ultrafast) "ultrafast" else "priority",
                )
                .toString()
                .toByteArray(Charsets.UTF_8),
        ),
        readTimeoutMillis = timing.inactivityMillis,
        cancellation = timing.cancellation,
        deadlineNanos = timing.deadlineNanos,
    ) { status, body ->
        if (status !in 200..299) throw refinementHttpFailure(status)
        readRefinement(provider, body.bufferedReader(), onText)
    }
}

/** Reads a server-sent-event stream until the provider's completion event. */
private fun readRefinement(
    provider: OAuthProvider,
    reader: BufferedReader,
    onText: (String) -> Unit,
): String {
    val output = StringBuilder()
    var event = ""
    val data = StringBuilder()
    var complete = false
    while (!complete) {
        val line = reader.readLine() ?: break
        if (line.isEmpty()) {
            if (data.isNotEmpty()) {
                val before = output.length
                complete = appendEvent(provider, event, data.toString(), output)
                if (output.length > before) onText(output.toString())
            }
            event = ""
            data.clear()
        } else if (line.startsWith("event:")) event = line.substringAfter(':').trim()
        else if (line.startsWith("data:")) data.append(line.substringAfter(':').trim())
    }
    if (!complete && data.isNotEmpty())
        complete = appendEvent(provider, event, data.toString(), output)
    if (!complete)
        throw ProviderFailure(
            ProviderFailureKind.InvalidResult,
            "Refinement ended before completion",
        )
    if (output.isEmpty())
        throw ProviderFailure(ProviderFailureKind.InvalidResult, "Refinement returned no text")
    return output.toString()
}

private fun claudeRequest(
    token: String,
    raw: String,
    vocabulary: List<VocabularyWord>,
    model: String,
    effort: String,
    context: RefinementContext,
    fast: Boolean,
    base: String,
): Request {
    val system = buildJsonArray {
        add(
            buildJsonObject {
                put("type", JsonPrimitive("text"))
                put(
                    "text",
                    JsonPrimitive("You are Claude Code, Anthropic's official CLI for Claude."),
                )
            }
        )
        add(
            buildJsonObject {
                put("type", JsonPrimitive("text"))
                put("text", JsonPrimitive(refinementSystemPrompt(context)))
            }
        )
    }
    val body = buildJsonObject {
        put("model", JsonPrimitive(model))
        put("max_tokens", JsonPrimitive(4096))
        put("stream", JsonPrimitive(true))
        if (fast) put("speed", JsonPrimitive("fast"))
        if (modelSupportsAdaptiveEffort(model)) {
            put(
                "thinking",
                buildJsonObject {
                    put("type", JsonPrimitive("adaptive"))
                    put("display", JsonPrimitive("omitted"))
                },
            )
            put(
                "output_config",
                buildJsonObject { put("effort", JsonPrimitive(anthropicEffort(model, effort))) },
            )
        }
        put("system", system)
        put(
            "messages",
            buildJsonArray {
                add(
                    buildJsonObject {
                        put("role", JsonPrimitive("user"))
                        put(
                            "content",
                            claudeContent(
                                refinementUserMessage(
                                    raw,
                                    vocabulary,
                                    context.bindingAliases,
                                    context.selectedText,
                                ),
                                context,
                            ),
                        )
                    }
                )
            },
        )
    }
    val id = UUID.randomUUID().toString()
    return Request.Builder()
        .url("$base/messages")
        .header("Authorization", "Bearer $token")
        .header("anthropic-version", "2023-06-01")
        .header(
            "anthropic-beta",
            if (fast) "claude-code-20250219,oauth-2025-04-20,fast-mode-2026-02-01"
            else "claude-code-20250219,oauth-2025-04-20",
        )
        .header("User-Agent", "claude-cli/unknown (external, cli)")
        .header("x-app", "cli")
        .header("x-claude-code-session-id", id)
        .header("x-client-request-id", id)
        .post(body.toString().toRequestBody("application/json".toMediaType()))
        .build()
}

private fun chatGptBody(
    raw: String,
    vocabulary: List<VocabularyWord>,
    model: String,
    effort: String,
    context: RefinementContext,
    serviceTier: String?,
) = buildJsonObject {
    put("model", JsonPrimitive(model))
    // GPT-6.1 Sol refuses "none"; low is the least effort it takes, as on the desktop.
    val sentEffort =
        if (effort == "none" && model.trim().lowercase().startsWith("gpt-6.1-sol")) "low"
        else effort
    put("reasoning", buildJsonObject { put("effort", JsonPrimitive(sentEffort)) })
    put("instructions", JsonPrimitive(refinementSystemPrompt(context)))
    put("stream", JsonPrimitive(true))
    put("store", JsonPrimitive(false))
    // chatgpt.com rejects "fast" ("Unsupported service_tier: fast"); "priority" is its fast tier.
    if (serviceTier != null) put("service_tier", JsonPrimitive(serviceTier))
    put(
        "input",
        buildJsonArray {
            add(
                buildJsonObject {
                    put("role", JsonPrimitive("user"))
                    put(
                        "content",
                        chatGptContent(
                            refinementUserMessage(
                                raw,
                                vocabulary,
                                context.bindingAliases,
                                context.selectedText,
                            ),
                            context,
                        ),
                    )
                }
            )
        },
    )
}

/** The user message, with the screenshot as an image block after it when one was captured. */
private fun claudeContent(message: String, context: RefinementContext): JsonElement {
    val screenshot = context.screenshotJpeg ?: return JsonPrimitive(message)
    return buildJsonArray {
        add(
            buildJsonObject {
                put("type", JsonPrimitive("text"))
                put("text", JsonPrimitive(message))
            }
        )
        add(
            buildJsonObject {
                put("type", JsonPrimitive("image"))
                put(
                    "source",
                    buildJsonObject {
                        put("type", JsonPrimitive("base64"))
                        put("media_type", JsonPrimitive(SCREENSHOT_MEDIA_TYPE))
                        put("data", JsonPrimitive(screenshot))
                    },
                )
            }
        )
    }
}

/** As [claudeContent], in the Responses API's input_text and low-detail input_image items. */
private fun chatGptContent(message: String, context: RefinementContext): JsonElement {
    val screenshot = context.screenshotJpeg ?: return JsonPrimitive(message)
    return buildJsonArray {
        add(
            buildJsonObject {
                put("type", JsonPrimitive("input_text"))
                put("text", JsonPrimitive(message))
            }
        )
        add(
            buildJsonObject {
                put("type", JsonPrimitive("input_image"))
                put("image_url", JsonPrimitive("data:$SCREENSHOT_MEDIA_TYPE;base64,$screenshot"))
                put("detail", JsonPrimitive("low"))
            }
        )
    }
}

private const val SCREENSHOT_MEDIA_TYPE = "image/jpeg"

private fun OAuthTokens.accountId(): String? = runCatching {
    val claims = String(Base64.getUrlDecoder().decode(idToken.split('.')[1]))
    val json = Json.parseToJsonElement(claims) as JsonObject
    (json["https://api.openai.com/auth"] as? JsonObject)
        ?.get("chatgpt_account_id")
        ?.jsonPrimitive
        ?.content
}
    .getOrNull()

private fun appendEvent(
    provider: OAuthProvider,
    name: String,
    data: String,
    output: StringBuilder,
): Boolean {
    val json =
        runCatching { Json.parseToJsonElement(data) as JsonObject }.getOrNull() ?: return false
    if (name == "error")
        throw ProviderFailure(
            streamedErrorKind(json["error"] as? JsonObject ?: json),
            "Refinement provider rejected the request",
        )
    // An incomplete response hit a token limit or a filter: the text is cut short.
    if (name == "response.incomplete")
        throw RefinementStopped(ProviderFailureKind.InvalidResult, "Refinement was cut short")
    if (name == "response.failed") {
        val response = json["response"] as? JsonObject ?: json
        throw RefinementStopped(
            streamedErrorKind(response["error"] as? JsonObject ?: response),
            "Refinement provider failed the response",
        )
    }
    if (provider == OAuthProvider.Claude && name == "message_delta") {
        val reason = (json["delta"] as? JsonObject)?.get("stop_reason")?.jsonPrimitive?.content
        if (reason != null && reason != "end_turn" && reason != "stop_sequence") {
            throw RefinementStopped(
                ProviderFailureKind.InvalidResult,
                "Refinement stopped before completion",
            )
        }
    }
    val delta =
        if (provider == OAuthProvider.Claude && name == "content_block_delta") {
            (json["delta"] as? JsonObject)?.get("text")?.jsonPrimitive?.content
        } else if (provider == OAuthProvider.ChatGpt && name == "response.output_text.delta") {
            json["delta"]?.jsonPrimitive?.content
        } else null
    if (delta != null) output.append(delta)
    return if (provider == OAuthProvider.Claude) name == "message_stop"
    else name == "response.completed"
}
