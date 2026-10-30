package app.speecher.android.update

import android.content.pm.PackageInfo
import app.speecher.android.BuildConfig
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
class ApkInstallerTest {
    private val app = RuntimeEnvironment.getApplication()
    private val update = ApkUpdate("0.2.0", "https://example.com/0.2.0.apk", "https://example.com")
    private val apk = File(app.cacheDir, "update.apk")

    /** The downloaded APK carries [versionCode]. */
    private fun downloaded(versionCode: Long) =
        shadowOf(app.packageManager)
            .setPackageArchiveInfo(apk.path, PackageInfo().apply { longVersionCode = versionCode })

    @Test
    fun `a lower version code sends the update to the release page instead of the installer`() {
        downloaded(BuildConfig.VERSION_CODE - 1L)
        assertEquals(
            UpdateState.InstallFailed(
                update,
                "This update can't replace the installed app. Uninstall Speecher, then install " +
                    "it from the release page.",
                manualInstall = true,
            ),
            downgradeFailure(app, update, apk),
        )

        downloaded(BuildConfig.VERSION_CODE.toLong())
        assertNull(downgradeFailure(app, update, apk))
    }
}
