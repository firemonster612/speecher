package app.speecher.protocol

import java.io.ByteArrayOutputStream
import java.util.zip.GZIPOutputStream
import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import okio.Buffer
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Test

class CodexTranscribeTest {
    @Test
    fun `WAV header matches the desktop container for 16 kHz mono PCM16`() {
        assertEquals(
            "52494646" +
                "28000000" +
                "57415645666d7420" +
                "10000000" +
                "0100" +
                "0100" +
                "803e0000" +
                "007d0000" +
                "0200" +
                "1000" +
                "64617461" +
                "04000000" +
                "0102ff7f",
            wavFromPcm16Mono(byteArrayOf(1, 2, -1, 127), 16000).joinToString("") {
                "%02x".format(it)
            },
        )
    }

    @Test
    fun `truncated or failed batch transcript falls back to the streamed one`() {
        assertEquals(
            "Hello there, friend.",
            preferredTranscript("Hello there, friend.", "hello there friend"),
        )
        assertEquals(
            "one two three four five",
            preferredTranscript("One two.", "one two three four five"),
        )
        assertEquals("streamed", preferredTranscript(null, "streamed"))
        assertEquals("streamed", preferredTranscript("", "streamed"))
    }

    @Test
    fun `uploads WAV as multipart file and reads chunked gzip JSON`() {
        MockWebServer().use { server ->
            val gzipped = ByteArrayOutputStream()
            GZIPOutputStream(gzipped).use { it.write("{\"text\":\" Hello. \"}".toByteArray()) }
            server.enqueue(
                MockResponse.Builder()
                    .addHeader("Content-Encoding", "gzip")
                    .chunkedBody(Buffer().write(gzipped.toByteArray()), 7)
                    .build()
            )
            server.start()
            val text =
                transcribeSpeech("token", byteArrayOf(1, 2), server.url("/transcribe").toString())
            assertEquals("Hello.", text)
            val request = server.takeRequest()
            assertEquals("Bearer token", request.headers["Authorization"])
            assertEquals(codexBrowserUserAgent, request.headers["User-Agent"])
            val boundary = request.headers["Content-Type"]!!.substringAfter("boundary=")
            assertEquals(
                "--$boundary\r\n" +
                    "Content-Disposition: form-data; name=\"file\"; filename=\"dictation.wav\"\r\n" +
                    "Content-Type: audio/wav\r\n\r\n",
                request.body!!.utf8().substringBefore("RIFF"),
            )
            assertEquals(
                "\r\n--$boundary--\r\n",
                request.body!!.utf8().takeLast(boundary.length + 8),
            )
        }
    }
}
