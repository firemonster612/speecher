package app.speecher.android.update

import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import okhttp3.OkHttpClient
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ReleaseCheckTest {
    @Test
    fun `returns the highest Android release APK, skipping desktop releases and prereleases`() {
        val releases =
            """[
              {"tag_name":"v0.3.0","prerelease":false,"assets":[{"name":"Speecher-x86_64.AppImage","browser_download_url":"https://example.com/appimage"}]},
              {"tag_name":"android-v0.3.0","prerelease":true,"assets":[{"name":"Speecher-0.3.0.apk","browser_download_url":"https://example.com/0.3.0.apk"}]},
              {"tag_name":"android-v0.1.18","prerelease":false,"assets":[{"name":"Speecher-0.1.18.apk","browser_download_url":"https://example.com/0.1.18.apk"}]},
              {"tag_name":"android-v0.2.0","prerelease":false,"assets":[{"name":"Speecher-0.2.0.apk","browser_download_url":"https://github.com/firemonster612/speecher/releases/download/android-v0.2.0/Speecher-0.2.0.apk"}]}
            ]"""
        MockWebServer().use { server ->
            repeat(2) { server.enqueue(MockResponse.Builder().body(releases).build()) }
            server.start()
            val url = server.url("/releases").toString()
            assertEquals(
                ApkUpdate(
                    "0.2.0",
                    "https://github.com/firemonster612/speecher/releases/download/android-v0.2.0/Speecher-0.2.0.apk",
                ),
                newerApk(OkHttpClient(), "0.1.18", url),
            )
            assertNull(newerApk(OkHttpClient(), "0.2.0", url))
        }
    }

    @Test
    fun `ignores the highest Android release until its APK is attached`() {
        MockWebServer().use { server ->
            server.enqueue(
                MockResponse.Builder()
                    .body("""[{"tag_name":"android-v0.3.0","prerelease":false,"assets":[]}]""")
                    .build()
            )
            server.start()
            assertNull(newerApk(OkHttpClient(), "0.2.0", server.url("/releases").toString()))
        }
    }
}
