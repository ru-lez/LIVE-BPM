package com.livebpm.android

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import kotlin.math.roundToInt

/**
 * Foreground "now" service. While detecting, it keeps the process alive (so the
 * engine + mic capture in [BpmViewModel] continue with the app backgrounded /
 * screen off) and posts an ongoing notification with the live BPM. Samsung's
 * One UI 7 surfaces this in the Now Bar; other phones show it on the lock screen.
 *
 * The app's audio lives with the Activity/ViewModel, so the notification's
 * lifetime is tied to the app being alive: [BpmViewModel] starts/stops this
 * service, and [onTaskRemoved] clears it if the task is swiped away.
 */
class LiveBpmService : Service() {

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        instance = this
        createChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        try {
            startForegroundCompat(buildNotification("--", 0f))
        } catch (e: Exception) {
            stopSelf()
        }
        return START_NOT_STICKY
    }

    /** Refresh the ongoing notification with the latest reading. */
    fun update(bpmText: String, confidence: Float) {
        val nm = getSystemService(NotificationManager::class.java) ?: return
        nm.notify(NOTIF_ID, buildNotification(bpmText, confidence))
    }

    override fun onTaskRemoved(rootIntent: Intent?) {
        // Audio can't continue once the app is gone — don't leave a stale bar.
        stopSelf()
        super.onTaskRemoved(rootIntent)
    }

    override fun onDestroy() {
        if (instance === this) instance = null
        super.onDestroy()
    }

    private fun startForegroundCompat(n: Notification) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE)
        } else {
            startForeground(NOTIF_ID, n)
        }
    }

    private fun buildNotification(bpmText: String, confidence: Float): Notification {
        val tap = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_IMMUTABLE,
        )
        val hasBpm = bpmText != "--"
        val pct = (confidence * 100).roundToInt()
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_stat_bpm)
            .setContentTitle(if (hasBpm) "$bpmText BPM" else "LiveBPM")
            .setContentText(if (hasBpm) "$pct% confidence" else "Listening…")
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setShowWhen(false)
            .setSilent(true)
            .setCategory(NotificationCompat.CATEGORY_SERVICE)
            .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setContentIntent(tap)
            .build()
    }

    private fun createChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "Live BPM",
                NotificationManager.IMPORTANCE_LOW,
            ).apply {
                description = "Shows the current BPM while detecting"
                setShowBadge(false)
            }
            getSystemService(NotificationManager::class.java)?.createNotificationChannel(channel)
        }
    }

    companion object {
        const val CHANNEL_ID = "livebpm_now"
        const val NOTIF_ID = 1

        @Volatile
        var instance: LiveBpmService? = null
            private set

        fun start(ctx: Context) {
            ContextCompat.startForegroundService(ctx, Intent(ctx, LiveBpmService::class.java))
        }

        fun stop(ctx: Context) {
            ctx.stopService(Intent(ctx, LiveBpmService::class.java))
        }
    }
}
