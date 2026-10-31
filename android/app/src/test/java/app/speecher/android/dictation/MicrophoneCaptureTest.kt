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
    fun `a resume waits for the paused capture to release the microphone`() {
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
        val microphone = Microphone(app)
        val heard = Semaphore(0)
        // Each capture is still handling its audio when the pause stops it.
        fun capture() = thread {
            microphone.capture({ true }) { _, _ ->
                heard.release()
                Thread.sleep(100)
            }
        }

        val paused = capture()
        assertTrue(heard.tryAcquire(5, TimeUnit.SECONDS))
        microphone.stop()
        val resumed = capture()
        assertTrue(heard.tryAcquire(5, TimeUnit.SECONDS))
        microphone.stop()
        paused.join()
        resumed.join()

        assertEquals(listOf(0, 0), openAtStart)
    }
}
