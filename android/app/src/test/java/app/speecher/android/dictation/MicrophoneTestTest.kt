package app.speecher.android.dictation

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class MicrophoneTestTest {
    @Test
    fun `heard input is reported and stays reported once the test stops`() {
        val test = MicrophoneTest({ _, onAudio -> onAudio(ByteArray(3200), 0.4f) }, Runnable::run)
        test.start()
        assertFalse(test.running)
        assertEquals("Microphone input detected.", test.status)
    }

    @Test
    fun `five seconds without input brings the hint`() {
        val seen = mutableListOf<String?>()
        lateinit var test: MicrophoneTest
        test =
            MicrophoneTest(
                { _, onAudio ->
                    onAudio(ByteArray(159_998), 0f)
                    seen += test.status
                    onAudio(ByteArray(2), 0f)
                    seen += test.status
                },
                Runnable::run,
            )
        test.start()
        assertEquals(
            listOf(
                "Listening for microphone input…",
                "No input yet — check that the microphone isn't muted or turned off in Quick " +
                    "Settings.",
            ),
            seen,
        )
    }

    @Test
    fun `no access and an unusable microphone read differently`() {
        val denied = MicrophoneTest({ _, _ -> throw SecurityException() }, Runnable::run)
        denied.start()
        assertEquals("Microphone access is off.", denied.status)
        val broken = MicrophoneTest({ _, _ -> error("Microphone read failed: -3") }, Runnable::run)
        broken.start()
        assertEquals("Microphone unavailable", broken.status)
    }

    @Test
    fun `a dictation starting ends the test, and none starts while it listens`() {
        var dictating = false
        var chunks = 0
        val test =
            MicrophoneTest(
                { shouldContinue, onAudio ->
                    while (shouldContinue()) {
                        onAudio(ByteArray(3200), 0f)
                        if (++chunks == 3) dictating = true
                    }
                },
                Runnable::run,
            ) {
                dictating
            }
        test.start()
        assertEquals(3, chunks)
        assertFalse(test.running)
        test.start()
        assertEquals(3, chunks)
    }
}
