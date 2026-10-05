package app.speecher.android.update

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import androidx.core.content.IntentCompat
import app.speecher.android.BuildConfig
import app.speecher.android.MainActivity
import java.io.File
import okhttp3.OkHttpClient
import okhttp3.Request

/** The MainActivity extra carrying the status Android refused an update with. */
const val INSTALL_STATUS = "install_status"

/**
 * Downloads [update]'s APK to [file] once the user chooses it, reporting each whole percent when
 * the size is known. Call on a worker thread.
 */
fun downloadApk(http: OkHttpClient, update: ApkUpdate, file: File, onProgress: (Int) -> Unit) {
    http.newCall(Request.Builder().url(update.downloadUrl).build()).execute().use { response ->
        if (!response.isSuccessful) error("Could not download APK: HTTP ${response.code}")
        val total = response.body.contentLength()
        response.body.byteStream().use { input ->
            file.outputStream().use { output ->
                val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                var copied = 0L
                var percent = -1
                while (true) {
                    val read = input.read(buffer)
                    if (read < 0) break
                    output.write(buffer, 0, read)
                    copied += read
                    if (total <= 0) continue
                    val now = (copied * 100 / total).toInt()
                    if (now == percent) continue
                    percent = now
                    onProgress(now)
                }
            }
        }
    }
}

/** Hands a downloaded APK to Android's installer, which answers [InstallResultReceiver]. */
fun installApk(context: Context, apk: File) {
    val installer = context.packageManager.packageInstaller
    val params =
        PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL).apply {
            setAppPackageName(BuildConfig.APPLICATION_ID)
            // An app updating itself installs without a confirmation prompt. Android falls back
            // to the prompt when it does not allow that, which InstallResultReceiver shows.
            setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_NOT_REQUIRED)
        }
    val id = installer.createSession(params)
    try {
        installer.openSession(id).use { session ->
            session.openWrite("speecher.apk", 0, apk.length()).use { output ->
                apk.inputStream().use { it.copyTo(output) }
                session.fsync(output)
            }
            val callback =
                PendingIntent.getBroadcast(
                    context,
                    id,
                    Intent(context, InstallResultReceiver::class.java)
                        .setAction("app.speecher.android.INSTALL_RESULT"),
                    PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE,
                )
            session.commit(callback.intentSender)
        }
    } catch (error: Exception) {
        installer.abandonSession(id)
        throw error
    }
}

/** What Home says when Android refuses [update] with a PackageInstaller [status]. */
fun installFailure(update: ApkUpdate, status: Int): UpdateState.InstallFailed =
    when (status) {
        PackageInstaller.STATUS_FAILURE_ABORTED ->
            UpdateState.InstallFailed(update, "The update was canceled.")
        PackageInstaller.STATUS_FAILURE_STORAGE ->
            UpdateState.InstallFailed(update, "Not enough storage to install the update.")
        // Usually a build signed with another key, which Android only installs after an uninstall.
        PackageInstaller.STATUS_FAILURE_CONFLICT ->
            UpdateState.InstallFailed(
                update,
                "This update can't replace the installed app. Uninstall Speecher, then install " +
                    "it from the release page.",
                manualInstall = true,
            )
        else -> UpdateState.InstallFailed(update, "Couldn't install the update.")
    }

class InstallResultReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        when (val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, -1)) {
            // Android replaces the app, ending this process.
            PackageInstaller.STATUS_SUCCESS -> Unit
            PackageInstaller.STATUS_PENDING_USER_ACTION -> {
                val confirmation =
                    IntentCompat.getParcelableExtra(intent, Intent.EXTRA_INTENT, Intent::class.java)
                confirmation?.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)?.let(context::startActivity)
            }
            else ->
                context.startActivity(
                    Intent(context, MainActivity::class.java)
                        .putExtra(INSTALL_STATUS, status)
                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                )
        }
    }
}
