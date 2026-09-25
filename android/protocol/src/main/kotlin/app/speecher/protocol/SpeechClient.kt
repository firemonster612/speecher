package app.speecher.protocol

internal fun isAuthenticationError(detail: String): Boolean =
    listOf("401", "403", "unauthorized", "forbidden").any { detail.contains(it, ignoreCase = true) }

/**
 * Close codes that mean the server went away or restarted, not that it refused the session: going
 * away, internal error, service restart, try again later. A stream closed with one of these can be
 * reopened.
 */
internal fun isRetryableClose(code: Int): Boolean = code in setOf(1001, 1011, 1012, 1013)

sealed interface SpeechEvent {
    data object Connected : SpeechEvent

    data class Partial(val text: String) : SpeechEvent

    data class Final(val text: String) : SpeechEvent

    data object Completed : SpeechEvent

    /** [retryable] marks a dropped connection or transient provider error a new stream may fix. */
    data class Failed(
        val authentication: Boolean,
        val detail: String = "",
        val retryable: Boolean = false,
    ) : SpeechEvent
}

interface SpeechClient {
    fun sendAudio(pcm: ByteArray)

    fun stop()

    fun cancel()
}
