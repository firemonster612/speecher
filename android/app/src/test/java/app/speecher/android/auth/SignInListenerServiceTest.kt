package app.speecher.android.auth

import android.content.pm.ServiceInfo
import app.speecher.android.dictation.Provider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
class SignInListenerServiceTest {
    private val app = RuntimeEnvironment.getApplication()

    private fun runStartedService(): SignInListenerService {
        val started = requireNotNull(shadowOf(app).nextStartedService)
        return Robolectric.buildService(SignInListenerService::class.java, started)
            .create()
            .startCommand(0, 1)
            .get()
    }

    @Test
    fun `an attempt that ends before the service starts still stops it`() {
        SignInListenerService.start(app, Provider.ChatGpt) {}
        SignInListenerService.stop(app)
        assertNull(shadowOf(app).nextStoppedService)
        val service = runStartedService()
        assertEquals(
            "Signing in to ChatGPT",
            shadowOf(service).lastForegroundNotification.extras.getString("android.title"),
        )
        assertTrue(shadowOf(service).isStoppedBySelf)
    }

    @Test
    fun `the time limit ends the attempt and the attempt's end stops the service`() {
        var timedOut = false
        SignInListenerService.start(app, Provider.Claude) { timedOut = true }
        val service = runStartedService()
        service.onTimeout(1, ServiceInfo.FOREGROUND_SERVICE_TYPE_SHORT_SERVICE)
        assertTrue(timedOut)
        assertTrue(shadowOf(service).isStoppedBySelf)
        SignInListenerService.stop(app)
        assertEquals(
            SignInListenerService::class.java.name,
            shadowOf(app).nextStoppedService.component?.className,
        )
    }
}
