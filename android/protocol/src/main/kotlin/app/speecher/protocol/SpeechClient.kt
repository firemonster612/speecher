package app.speecher.protocol

sealed interface SpeechEvent {
    data object Connected : SpeechEvent

    data class Partial(val text: String) : SpeechEvent

    data class Final(val text: String) : SpeechEvent

    data object Completed : SpeechEvent

    data class Failed(val authentication: Boolean) : SpeechEvent
}

interface SpeechClient {
    fun sendAudio(pcm: ByteArray)

    fun stop()

    fun cancel()
}
