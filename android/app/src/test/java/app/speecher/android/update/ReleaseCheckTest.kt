package app.speecher.android.update

import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import okhttp3.OkHttpClient
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ReleaseCheckTest {
    private fun releases(json: String): List<AndroidRelease> =
        MockWebServer().use { server ->
            server.enqueue(MockResponse.Builder().body(json).build())
            server.start()
            androidReleases(OkHttpClient(), server.url("/releases").toString())
        }

    private val list =
        releases(
            """[
              {"tag_name":"v0.3.0","prerelease":false,"body":"desktop","html_url":"https://example.com/v0.3.0","assets":[{"name":"Speecher-x86_64.AppImage","browser_download_url":"https://example.com/appimage"}]},
              {"tag_name":"android-v0.3.0","prerelease":true,"body":"","html_url":"https://example.com/pre","assets":[{"name":"Speecher-0.3.0.apk","browser_download_url":"https://example.com/0.3.0.apk"}]},
              {"tag_name":"android-nightly","prerelease":true,"body":"Version: 0.2.1-nightly.57+g1a2b3c4\nCommit: 1a2b3c4\n\nThis Nightly Build is untested.","html_url":"https://example.com/nightly","assets":[{"name":"Speecher-nightly.apk","browser_download_url":"https://example.com/nightly.apk"}]},
              {"tag_name":"android-v0.1.18","prerelease":false,"body":"## Fixed\n\n- Old","html_url":"https://example.com/0.1.18","assets":[{"name":"Speecher-0.1.18.apk","browser_download_url":"https://example.com/0.1.18.apk"}]},
              {"tag_name":"android-v0.2.0","prerelease":false,"body":"## Added\n\n- Updates","html_url":"https://github.com/firemonster612/speecher/releases/tag/android-v0.2.0","assets":[{"name":"Speecher-0.2.0.apk","browser_download_url":"https://github.com/firemonster612/speecher/releases/download/android-v0.2.0/Speecher-0.2.0.apk"}]}
            ]"""
        )

    @Test
    fun `Stable offers the highest Android release, skipping desktop releases and prereleases`() {
        assertEquals(
            ApkUpdate(
                "0.2.0",
                "https://github.com/firemonster612/speecher/releases/download/android-v0.2.0/Speecher-0.2.0.apk",
                "https://github.com/firemonster612/speecher/releases/tag/android-v0.2.0",
            ),
            newerApk(list, UpdateChannel.Stable, "0.1.18"),
        )
        assertNull(newerApk(list, UpdateChannel.Stable, "0.2.0"))
    }

    @Test
    fun `Nightly offers the nightly by the version its notes give, until a release passes it`() {
        assertEquals(
            ApkUpdate(
                "0.2.1-nightly.57+g1a2b3c4",
                "https://example.com/nightly.apk",
                "https://example.com/nightly",
            ),
            newerApk(list, UpdateChannel.Nightly, "0.2.0"),
        )
        assertNull(newerApk(list, UpdateChannel.Nightly, "0.2.1-nightly.57+g1a2b3c4"))
        assertEquals(
            "0.3.0",
            newerApk(
                    list + AndroidRelease("0.3.0", "", "", "https://example.com/0.3.0.apk"),
                    UpdateChannel.Nightly,
                    "0.2.1-nightly.57+g1a2b3c4",
                )
                ?.version,
        )
    }

    @Test
    fun `ignores the highest Android release until its APK is attached`() {
        val unattached =
            releases("""[{"tag_name":"android-v0.3.0","prerelease":false,"assets":[]}]""")
        assertNull(newerApk(unattached + list, UpdateChannel.Stable, "0.2.0"))
    }

    @Test
    fun `a nightly sorts above the release before it and below the one it leads up to`() {
        assertTrue(compareVersions("0.2.1-nightly.57+g1a2b3c4", "0.2.0") > 0)
        assertTrue(compareVersions("0.2.1-nightly.57+g1a2b3c4", "0.2.1") < 0)
        assertTrue(compareVersions("0.2.1-nightly.100+gfff", "0.2.1-nightly.57+g1a2b3c4") > 0)
        assertTrue(compareVersions("0.10.0", "0.9.9") > 0)
        assertEquals(0, compareVersions("v0.2.0", "0.2"))
    }

    @Test
    fun `What's New covers the releases since the last one seen, or else the installed one`() {
        assertEquals(
            listOf("0.2.0", "0.1.18"),
            releaseNotes(list, "0.1.0", "0.2.0").map { it.version },
        )
        assertEquals(listOf("0.2.0"), releaseNotes(list, null, "0.2.0").map { it.version })
        assertEquals(listOf("0.2.0"), releaseNotes(list, "0.2.0", "0.2.0").map { it.version })
        assertEquals("## Added\n\n- Updates", releaseNotes(list, null, "0.2.0").single().notes)
    }

    @Test
    fun `What's New waits for an upgrade, and stays until seen`() {
        assertEquals("0.1.18", whatsNewSince("0.1.18", "0.2.0", null))
        assertNull(whatsNewSince(null, "0.2.0", null))
        assertNull(whatsNewSince("0.2.0", "0.2.0", null))
        assertEquals("0.1.0", whatsNewSince("0.2.0", "0.2.0", "0.1.0"))
    }

    @Test
    fun `a nightly reads as its build number and commit, as on the desktop`() {
        assertEquals("nightly build 57 (g1a2b3c4)", versionDisplay("0.2.1-nightly.57+g1a2b3c4"))
        assertEquals("0.2.0", versionDisplay("0.2.0"))
    }
}
