package app.speecher.protocol

import java.io.BufferedReader
import java.net.SocketTimeoutException
import java.util.Base64
import java.util.UUID
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
    vocabulary: List<String>,
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
    /** Receives the refined text so far each time the stream adds to it. */
    onText: (String) -> Unit = {},
): String {
    var streamed = false
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
            endpointBase,
        ) {
            streamed = true
            onText(it)
        }
    // A model without vision rejects the image, and an oversized one is refused; the dictation
    // still deserves a text-only pass. Other failures would fail again, so they are not retried.
    // Both statuses arrive before any text, so the retry never replays streamed output.
    fun refineAtSpeed(fast: Boolean): String {
        if (context.screenshotJpeg == null) return refine(context, fast)
        return try {
            refine(context, fast)
        } catch (failure: RefinementHttpError) {
            if (failure.status !in IMAGE_REJECTED_STATUSES) throw failure
            refine(context.copy(screenshotJpeg = null), fast)
        }
    }
    if (
        fastMode == null ||
            !fastMode.get() ||
            provider == OAuthProvider.Claude && !modelSupportsFastMode(model)
    )
        return refineAtSpeed(fast = false)
    // Fast mode must never cost the user their insert: anything that fails it before text streams
    // gets one standard-speed try, as the desktop's StreamingRefinement does.
    return try {
        refineAtSpeed(fast = true)
    } catch (failure: Exception) {
        if (streamed || failure is RefinementStopped) throw failure
        // A stall says nothing about fast mode, so like the desktop only a refusal latches it off.
        refineAtSpeed(fast = false).also {
            if (failure !is SocketTimeoutException) fastMode.set(false)
        }
    }
}

/**
 * Anthropic's fast mode is a research preview limited to Opus 5 and Opus 4.8; other models fail
 * every request that asks for it.
 */
fun modelSupportsFastMode(model: String): Boolean =
    model.lowercase().let { it.contains("opus-5") || it.contains("opus-4-8") }

/** A refinement request the provider answered with a non-2xx [status]. */
class RefinementHttpError(val status: Int) :
    IllegalStateException("Refinement failed with HTTP $status")

/**
 * The provider ended the response itself (failed, incomplete, or an unexpected stop reason). The
 * desktop retries those at neither speed, so neither does the fast-mode fallback.
 */
private class RefinementStopped(message: String) : IllegalStateException(message)

/** Bad request and payload too large: what a provider answers when it will not take the image. */
private val IMAGE_REJECTED_STATUSES = setOf(400, 413)

private fun refineOnce(
    http: OkHttpClient,
    provider: OAuthProvider,
    tokens: OAuthTokens,
    rawTranscript: String,
    vocabulary: List<String>,
    model: String,
    effort: String,
    context: RefinementContext,
    fast: Boolean,
    endpointBase: String,
    onText: (String) -> Unit,
): String {
    val base = endpointBase.trimEnd('/')
    if (provider == OAuthProvider.Claude) {
        http
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
            .execute()
            .use { response ->
                if (!response.isSuccessful) throw RefinementHttpError(response.code)
                return readRefinement(provider, response.body.charStream().buffered(), onText)
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
            chatGptBody(rawTranscript, vocabulary, model, effort, context, fast)
                .toString()
                .toByteArray(Charsets.UTF_8),
        ),
    ) { status, body ->
        if (status !in 200..299) throw RefinementHttpError(status)
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
    if (!complete) error("Refinement stream ended before completion")
    if (output.isEmpty()) error("Refinement returned no text")
    return output.toString()
}

private fun claudeRequest(
    token: String,
    raw: String,
    vocabulary: List<String>,
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
                put("text", JsonPrimitive(dictationSystemPrompt(context)))
            }
        )
    }
    val body = buildJsonObject {
        put("model", JsonPrimitive(model))
        put("max_tokens", JsonPrimitive(4096))
        put("stream", JsonPrimitive(true))
        if (fast) put("speed", JsonPrimitive("fast"))
        put(
            "thinking",
            buildJsonObject {
                put("type", JsonPrimitive("adaptive"))
                put("display", JsonPrimitive("omitted"))
            },
        )
        put("output_config", buildJsonObject { put("effort", JsonPrimitive(effort)) })
        put("system", system)
        put(
            "messages",
            buildJsonArray {
                add(
                    buildJsonObject {
                        put("role", JsonPrimitive("user"))
                        put(
                            "content",
                            claudeContent(refinementUserMessage(raw, vocabulary), context),
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
    vocabulary: List<String>,
    model: String,
    effort: String,
    context: RefinementContext,
    fast: Boolean,
) = buildJsonObject {
    put("model", JsonPrimitive(model))
    put("reasoning", buildJsonObject { put("effort", JsonPrimitive(effort)) })
    put("instructions", JsonPrimitive(dictationSystemPrompt(context)))
    put("stream", JsonPrimitive(true))
    put("store", JsonPrimitive(false))
    // chatgpt.com rejects "fast" ("Unsupported service_tier: fast"); "priority" is its fast tier.
    if (fast) put("service_tier", JsonPrimitive("priority"))
    put(
        "input",
        buildJsonArray {
            add(
                buildJsonObject {
                    put("role", JsonPrimitive("user"))
                    put("content", chatGptContent(refinementUserMessage(raw, vocabulary), context))
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
    if (name == "error") error("Refinement provider rejected the request")
    if (name == "response.failed" || name == "response.incomplete") {
        throw RefinementStopped("Refinement provider ended the response: $name")
    }
    if (provider == OAuthProvider.Claude && name == "message_delta") {
        val reason = (json["delta"] as? JsonObject)?.get("stop_reason")?.jsonPrimitive?.content
        if (reason != null && reason != "end_turn" && reason != "stop_sequence") {
            throw RefinementStopped("Refinement stopped before completion")
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
