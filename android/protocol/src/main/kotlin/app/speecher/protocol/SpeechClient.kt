package app.speecher.protocol

/** The spoken language code that lets the speech service detect the language itself. */
const val AUTOMATIC_LANGUAGE = "auto"

/** English's spoken language code, every install's default. */
const val ENGLISH_LANGUAGE = "en"

internal fun isAuthenticationError(detail: String): Boolean =
    listOf("401", "403", "unauthorized", "forbidden").any { detail.contains(it, ignoreCase = true) }

/**
 * Whether a close the client did not ask for, on a live stream it has not stopped, is the server
 * ending the session on purpose: a normal closure, or a close frame without a code. The stream then
 * completes and a new session carries on. Any other unrequested close is a dropped connection,
 * retryable whatever its code, as on the desktop.
 */
internal fun endsSession(closeCode: Int): Boolean = closeCode == 1000 || closeCode == 1005

sealed interface SpeechEvent {
    data object Connected : SpeechEvent

    data class Partial(val text: String) : SpeechEvent

    data class Final(val text: String) : SpeechEvent

    /**
     * The stream ended cleanly. After [SpeechClient.stop] the transcript is final; before it, the
     * provider ended the session itself (ChatGPT does at its session_ttl_ms) and a new one can
     * carry on.
     */
    data object Completed : SpeechEvent

    /**
     * [retryable] marks a dropped connection or transient provider error a new stream may fix.
     * [detail] is diagnostic and never shown.
     */
    data class Failed(
        val kind: ProviderFailureKind,
        val detail: String = "",
        val retryable: Boolean = false,
    ) : SpeechEvent
}

/** A refused WebSocket upgrade by its HTTP [statusCode], any other transport error by [error]. */
internal fun transportFailureKind(error: Throwable, statusCode: Int?): ProviderFailureKind =
    statusCode?.let(::failureKindForHttpStatus) ?: failureKind(error)

interface SpeechClient {
    fun sendAudio(pcm: ByteArray)

    fun stop()

    fun cancel()
}
