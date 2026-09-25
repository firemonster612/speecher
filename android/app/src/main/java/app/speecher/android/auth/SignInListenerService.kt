package app.speecher.android.auth

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import app.speecher.android.R
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.label

private const val CHANNEL = "sign-in"
private const val NOTIFICATION_ID = 1
private const val PROVIDER_EXTRA = "provider"

/**
 * Keeps the process out of Android's app freezer while a browser sign-in waits for its loopback
 * redirect: a frozen process keeps the socket bound but never answers it. `shortService` allows
 * about three minutes, the same as [SignIn]'s listener deadline. Driven from the main thread
 * through [start] and [stop].
 */
class SignInListenerService : Service() {
    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val provider = Provider.valueOf(requireNotNull(intent?.getStringExtra(PROVIDER_EXTRA)))
        getSystemService(NotificationManager::class.java)
            .createNotificationChannel(
                NotificationChannel(CHANNEL, "Sign-in", NotificationManager.IMPORTANCE_LOW)
            )
        val notification =
            Notification.Builder(this, CHANNEL)
                .setSmallIcon(R.drawable.ic_launcher_foreground)
                .setContentTitle("Signing in to ${provider.label}")
                .setContentText("Return to Speecher when the browser says you're done.")
                .setOngoing(true)
                .build()
        // Each start renews shortService's time limit, so a new attempt gets the full three
        // minutes.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            startForeground(
                NOTIFICATION_ID,
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_SHORT_SERVICE,
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
        unstarted--
        // Only this start: a newer one still on its way decides for itself.
        if (onTimedOut == null) stopSelf(startId)
        return START_NOT_STICKY
    }

    // Android 14 calls only this overload; 15 and later call both, so act on one of them.
    override fun onTimeout(startId: Int) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.VANILLA_ICE_CREAM) timedOut()
    }

    override fun onTimeout(startId: Int, fgsType: Int) = timedOut()

    private fun timedOut() {
        stopSelf()
        onTimedOut?.invoke()
    }

    companion object {
        /** Set while an attempt wants the service; ends that attempt when the time limit fires. */
        private var onTimedOut: (() -> Unit)? = null

        /** Starts not yet through [onStartCommand]. */
        private var unstarted = 0

        /** Call while Speecher is in front: Android refuses a foreground service otherwise. */
        fun start(context: Context, provider: Provider, onTimedOut: () -> Unit) {
            this.onTimedOut = onTimedOut
            context.startForegroundService(
                Intent(context, SignInListenerService::class.java)
                    .putExtra(PROVIDER_EXTRA, provider.name)
            )
            unstarted++
        }

        fun stop(context: Context) {
            onTimedOut = null
            // Stopping before startForeground has run crashes the app, so a start still on its
            // way stops itself in onStartCommand instead.
            if (unstarted == 0) {
                context.stopService(Intent(context, SignInListenerService::class.java))
            }
        }
    }
}
