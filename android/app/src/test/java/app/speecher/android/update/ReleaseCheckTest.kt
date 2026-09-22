package app.speecher.android.update

import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import okhttp3.OkHttpClient
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ReleaseCheckTest {
    @Test
    fun `returns newer release APK and ignores releases without an APK`() {
        MockWebServer().use { server ->
            server.enqueue(
                MockResponse.Builder()
                    .body(
                        """{"tag_name":"v0.2.0","assets":[{"name":"Speecher.apk","browser_download_url":"https://github.com/firemonster612/speecher/releases/download/v0.2.0/Speecher.apk"}]}"""
                    )
                    .build()
            )
            server.enqueue(
                MockResponse.Builder().body("""{"tag_name":"v0.3.0","assets":[]}""").build()
            )
            server.start()
            val check = ReleaseCheck(OkHttpClient())
            assertEquals(
                ApkUpdate(
                    "0.2.0",
                    "https://github.com/firemonster612/speecher/releases/download/v0.2.0/Speecher.apk",
                ),
                check.newerApk("0.1.0", server.url("/latest").toString()),
            )
            assertNull(check.newerApk("0.2.0", server.url("/latest").toString()))
        }
    }
}
