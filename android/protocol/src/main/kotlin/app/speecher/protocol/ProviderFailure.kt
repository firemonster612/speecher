package app.speecher.protocol

import java.io.IOException
import java.net.SocketTimeoutException
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive

/**
 * Why a speech or cleanup provider could not do its job, as far as the provider can tell. The kind
 * decides whether a dictation moves on to the next provider, and what the panel says. The desktop's
 * ProviderFailureKind.
 */
enum class ProviderFailureKind {
    /** Not classified. Never moves on. */
    Other,
    /** Missing configuration or sign-in. */
    Unavailable,
    /** The service rejected the sign-in (401, 403, a refused refresh). */
    Authentication,
    /** Offline, refused, DNS, TLS: the request never got an answer, or lost it. */
    Network,
    Timeout,
    /** 5xx, or an error event the server streamed. */
    Server,
    /** 429, or a quota the server streamed. */
    RateLimited,
    /** An empty, truncated or cut-short answer. */
    InvalidResult,
    Cancelled;

    /** Whether a failure of this kind sends a dictation on to the next provider in its chain. */
    val permitsFallback: Boolean
        get() = this != Other && this != InvalidResult && this != Cancelled
}

/**
 * A provider request that failed as [kind]. The message is diagnostic: the panel never shows it,
 * and it never holds a token or a response body. [httpStatus] is 0 when there was none.
 */
open class ProviderFailure(
    val kind: ProviderFailureKind,
    message: String,
    val httpStatus: Int = 0,
    cause: Throwable? = null,
) : Exception(message, cause)

/** 401 and 403 are a rejected sign-in, 429 a rate limit, 5xx the server's own failure. */
fun failureKindForHttpStatus(status: Int): ProviderFailureKind =
    when (status) {
        401,
        403 -> ProviderFailureKind.Authentication
        429 -> ProviderFailureKind.RateLimited
        in 500..599 -> ProviderFailureKind.Server
        else -> ProviderFailureKind.Other
    }

/**
 * Any exception a provider request threw: its own kind when it is a [ProviderFailure], a timeout or
 * a network failure when the connection gave out, and Other otherwise.
 */
fun failureKind(error: Throwable): ProviderFailureKind =
    when (error) {
        is ProviderFailure -> error.kind
        is SocketTimeoutException -> ProviderFailureKind.Timeout
        is IOException -> ProviderFailureKind.Network
        else -> ProviderFailureKind.Other
    }

/**
 * An error object a service streamed after accepting the request. Its specific code, a string or a
 * number, decides first, so `{"type":"invalid_request_error","code":"invalid_api_key"}` is a
 * rejected sign-in; the generic type decides only when the code names no kind. An unknown error is
 * the server's failure, and one naming an invalid or missing part of the request is Other.
 */
fun streamedErrorKind(error: JsonObject): ProviderFailureKind =
    knownStreamedErrorKind(error.text("code"))
        ?: knownStreamedErrorKind(error.text("type"))
        ?: ProviderFailureKind.Server

private fun knownStreamedErrorKind(code: String): ProviderFailureKind? =
    when {
        code in AUTHENTICATION_CODES -> ProviderFailureKind.Authentication
        code in RATE_LIMIT_CODES -> ProviderFailureKind.RateLimited
        "invalid" in code || "not_found" in code -> ProviderFailureKind.Other
        else -> null
    }

private val AUTHENTICATION_CODES =
    setOf("authentication_error", "permission_error", "invalid_api_key", "401", "403")

private val RATE_LIMIT_CODES =
    setOf("rate_limit_error", "rate_limit_exceeded", "insufficient_quota", "429")

private fun JsonObject.text(key: String): String =
    (this[key] as? JsonPrimitive)?.takeUnless { it is JsonNull }?.content.orEmpty()
