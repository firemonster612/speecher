package app.speecher.android

import android.content.Context
import android.provider.Settings
import android.view.inputmethod.EditorInfo
import androidx.core.content.edit
import app.speecher.android.dictation.ActiveDictation
import org.junit.Assert.assertEquals
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

    @Test
    fun `the keyboard starting after a dead process keeps the keyboard to return to`() {
        val app = RuntimeEnvironment.getApplication()
        app.getSharedPreferences("previous-ime", Context.MODE_PRIVATE).edit(commit = true) {
            putString("id", "com.example/.Keyboard")
        }
        Settings.Secure.putString(
            app.contentResolver,
            Settings.Secure.DEFAULT_INPUT_METHOD,
            speecherImeId(app),
        )

        // Android creates the keyboard before it may switch keyboards, so a switch back there
        // would do nothing but forget where to return.
        Robolectric.buildService(SpeecherImeService::class.java).create().destroy()

        assertEquals("com.example/.Keyboard", ImeSwap(app).previousId)
    }

    @Test
    fun `the keyboard picked with nothing dictating switches back`() {
        val app = RuntimeEnvironment.getApplication()
        app.getSharedPreferences("previous-ime", Context.MODE_PRIVATE).edit(commit = true) {
            putString("id", "com.example/.Keyboard")
        }
        val ime = Robolectric.buildService(SpeecherImeService::class.java).create().get()

        ime.onStartInput(EditorInfo(), false)

        // Switching back forgets the keyboard it returned to.
        assertNull(ImeSwap(app).previousId)
    }
}
