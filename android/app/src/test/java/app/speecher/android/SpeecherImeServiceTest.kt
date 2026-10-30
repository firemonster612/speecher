package app.speecher.android

import app.speecher.android.dictation.ActiveDictation
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment

@RunWith(RobolectricTestRunner::class)
class SpeecherImeServiceTest {
    @Test
    fun `destroying the keyboard ends its dictation`() {
        val ime = Robolectric.buildService(SpeecherImeService::class.java).create()
        ActiveDictation.start(RuntimeEnvironment.getApplication(), null)
        ime.destroy()
        assertNull(ActiveDictation.session)
    }
}
