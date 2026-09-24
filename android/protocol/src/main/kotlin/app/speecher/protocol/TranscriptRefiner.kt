package app.speecher.protocol

import java.io.BufferedReader
import java.util.Base64
import java.util.UUID
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
    /** Receives the refined text so far each time the stream adds to it. */
    onText: (String) -> Unit = {},
): String {
    val refine = { sent: RefinementContext ->
        refineOnce(
            http,
            provider,
            tokens,
            rawTranscript,
            vocabulary,
            model,
            effort,
            sent,
            endpointBase,
            onText,
        )
    }
    if (context.screenshotJpeg == null) return refine(context)
    // A model without vision rejects the image, and an oversized one is refused; the dictation
    // still deserves a text-only pass. Other failures would fail again, so they are not retried.
    // Both statuses arrive before any text, so the retry never replays streamed output.
    return try {
        refine(context)
    } catch (failure: RefinementHttpError) {
        if (failure.status !in IMAGE_REJECTED_STATUSES) throw failure
        refine(context.copy(screenshotJpeg = null))
    }
}

/** A refinement request the provider answered with a non-2xx [status]. */
class RefinementHttpError(val status: Int) :
    IllegalStateException("Refinement failed with HTTP $status")

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
            chatGptBody(rawTranscript, vocabulary, model, effort, context)
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
        .header("anthropic-beta", "claude-code-20250219,oauth-2025-04-20")
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
) = buildJsonObject {
    put("model", JsonPrimitive(model))
    put("reasoning", buildJsonObject { put("effort", JsonPrimitive(effort)) })
    put("instructions", JsonPrimitive(dictationSystemPrompt(context)))
    put("stream", JsonPrimitive(true))
    put("store", JsonPrimitive(false))
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
    if (name == "error" || name == "response.failed" || name == "response.incomplete") {
        error("Refinement provider rejected the request")
    }
    if (provider == OAuthProvider.Claude && name == "message_delta") {
        val reason = (json["delta"] as? JsonObject)?.get("stop_reason")?.jsonPrimitive?.content
        if (reason != null && reason != "end_turn" && reason != "stop_sequence") {
            error("Refinement stopped before completion")
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
