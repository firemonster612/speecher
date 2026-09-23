package app.speecher.protocol

import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.jsonPrimitive

/**
 * ChatGPT's batch speech-to-text pass: re-transcribes a whole dictation's 16 kHz mono PCM16 audio
 * in one request, which is more accurate than the streamed segments.
 */
fun transcribeSpeech(
    token: String,
    pcm: ByteArray,
    endpoint: String = "https://chatgpt.com/backend-api/transcribe",
): String {
    val response =
        httpPost(
            endpoint,
            mapOf("Authorization" to "Bearer $token", "User-Agent" to codexBrowserUserAgent),
            multipartFile("file", "dictation.wav", "audio/wav", wavFromPcm16Mono(pcm, 16000)),
        )
    if (response.status !in 200..299) error("Transcription failed with HTTP ${response.status}")
    val json = Json.parseToJsonElement(response.body.toString(Charsets.UTF_8)) as? JsonObject
    return json?.get("text")?.jsonPrimitive?.content?.trim()
        ?: error("Transcription response has no text")
}

/**
 * The batch endpoint transcribes only the first ~90 s of a long recording and returns the truncated
 * text as a success. A batch transcript far shorter than the streamed one means text was dropped,
 * not re-decoded, so the streamed transcript wins then, as it does when the batch pass failed.
 */
fun preferredTranscript(batch: String?, streamed: String): String =
    if (batch.isNullOrEmpty() || batch.length * 10 < streamed.length * 6) streamed else batch

fun wavFromPcm16Mono(pcm: ByteArray, rateHz: Int): ByteArray =
    ByteBuffer.allocate(44 + pcm.size)
        .order(ByteOrder.LITTLE_ENDIAN)
        .put("RIFF".toByteArray(Charsets.US_ASCII))
        .putInt(36 + pcm.size)
        .put("WAVEfmt ".toByteArray(Charsets.US_ASCII))
        .putInt(16)
        .putShort(1)
        .putShort(1)
        .putInt(rateHz)
        .putInt(rateHz * 2)
        .putShort(2)
        .putShort(16)
        .put("data".toByteArray(Charsets.US_ASCII))
        .putInt(pcm.size)
        .put(pcm)
        .array()
