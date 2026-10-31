package app.speecher.android.dictation

import android.Manifest
import android.media.AudioRecord
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.Semaphore
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf
import org.robolectric.shadows.ShadowAudioRecord

@RunWith(RobolectricTestRunner::class)
class MicrophoneCaptureTest {
    @After fun clearSource() = ShadowAudioRecord.clearSource()

    @Test
    fun `a capture waits for the last one, from any microphone, to release the recorder`() {
        val app = RuntimeEnvironment.getApplication()
        shadowOf(app).grantPermissions(Manifest.permission.RECORD_AUDIO)
        val recorders = CopyOnWriteArrayList<AudioRecord>()
        // How many other recorders were still open as each one started reading.
        val openAtStart = CopyOnWriteArrayList<Int>()
        ShadowAudioRecord.setSourceProvider { recorder ->
            openAtStart += recorders.count { it.state == AudioRecord.STATE_INITIALIZED }
            recorders += recorder
            object : ShadowAudioRecord.AudioRecordSource {
                override fun readInShortArray(
                    audio: ShortArray,
                    offset: Int,
                    size: Int,
                    blocking: Boolean,
                ): Int = size.also { Thread.sleep(5) }
            }
        }
        val heard = Semaphore(0)
        // Each capture is still handling its audio when it is stopped.
        fun capture(microphone: Microphone) = thread {
            microphone.capture({ true }) { _, _ ->
                heard.release()
                Thread.sleep(100)
            }
        }

        // A resume or the next session: each gets a microphone of its own.
        val first = Microphone(app)
        val firstCapture = capture(first)
        assertTrue(heard.tryAcquire(5, TimeUnit.SECONDS))
        first.stop()
        val next = Microphone(app)
        val nextCapture = capture(next)
        assertTrue(heard.tryAcquire(5, TimeUnit.SECONDS))
        next.stop()
        firstCapture.join()
        nextCapture.join()

        assertEquals(listOf(0, 0), openAtStart)
    }
}
