package app.speecher.android.update

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import androidx.core.content.IntentCompat
import app.speecher.android.BuildConfig
import okhttp3.OkHttpClient
import okhttp3.Request

/** Download and install only after the user chooses an [ApkUpdate]. Call on a worker thread. */
fun installApk(context: Context, http: OkHttpClient, update: ApkUpdate) {
    val response = http.newCall(Request.Builder().url(update.downloadUrl).build()).execute()
    response.use {
        if (!it.isSuccessful) error("Could not download APK: HTTP ${it.code}")
        val installer = context.packageManager.packageInstaller
        val params =
            PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL).apply {
                setAppPackageName(BuildConfig.APPLICATION_ID)
            }
        val id = installer.createSession(params)
        try {
            installer.openSession(id).use { session ->
                session.openWrite("speecher.apk", 0, it.body.contentLength()).use { output ->
                    it.body.byteStream().copyTo(output)
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
}

class InstallResultReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (
            intent.getIntExtra(PackageInstaller.EXTRA_STATUS, -1) ==
                PackageInstaller.STATUS_PENDING_USER_ACTION
        ) {
            val confirmation =
                IntentCompat.getParcelableExtra(intent, Intent.EXTRA_INTENT, Intent::class.java)
            confirmation?.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)?.let(context::startActivity)
        }
    }
}
