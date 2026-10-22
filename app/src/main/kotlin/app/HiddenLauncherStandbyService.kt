// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package app

import android.app.ActivityManager
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import androidx.core.content.ContextCompat
import data.AppSettingsPreferences
import features.logs.AndroidAppLogger

/**
 * HyperOS drops the dialer secret-code broadcast while this process is cached.
 * A silent foreground service keeps the process above that state so the dial
 * code can still open the app after the launcher icon is hidden.
 */
class HiddenLauncherStandbyService : Service() {
    override fun onBind(intent: Intent?) = null

    override fun onCreate() {
        super.onCreate()
        ensureChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val hidden = intent?.getBooleanExtra(ExtraHidden, false)
            ?: AppSettingsPreferences(this).load().hideLauncherIcon
        startForegroundCompat(buildNotification())
        if (!hidden || hasOtherForegroundService()) {
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf(startId)
            return START_NOT_STICKY
        }
        return START_STICKY
    }

    private fun ensureChannel() {
        val manager = getSystemService(NotificationManager::class.java)
        val channel = NotificationChannel(
            ChannelId,
            getString(R.string.settings_hidden_launcher_standby_channel),
            NotificationManager.IMPORTANCE_LOW,
        )
        channel.setSound(null, null)
        channel.enableVibration(false)
        channel.setShowBadge(false)
        manager.createNotificationChannel(channel)
    }

    private fun buildNotification(): Notification {
        val open = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        return Notification.Builder(this, ChannelId)
            .setSmallIcon(R.drawable.ic_qs_proxy)
            .setContentTitle(getString(R.string.settings_hidden_launcher_standby_title))
            .setContentText(getString(R.string.settings_hidden_launcher_standby_text))
            .setContentIntent(open)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setSilent(true)
            .setShowWhen(false)
            .setLocalOnly(true)
            .setCategory(Notification.CATEGORY_SERVICE)
            .setVisibility(Notification.VISIBILITY_SECRET)
            .build()
    }

    private fun startForegroundCompat(notification: Notification) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            startForeground(NotificationId, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE)
        } else {
            startForeground(NotificationId, notification)
        }
    }

    companion object {
        fun reconcile(context: Context, hidden: Boolean, assumeNoOtherForeground: Boolean = false) {
            val appContext = context.applicationContext
            if (!hidden || (!assumeNoOtherForeground && appContext.hasOtherForegroundService())) {
                appContext.stopService(Intent(appContext, HiddenLauncherStandbyService::class.java))
                return
            }
            runCatching {
                ContextCompat.startForegroundService(
                    appContext,
                    Intent(appContext, HiddenLauncherStandbyService::class.java)
                        .putExtra(ExtraHidden, true),
                )
            }.onFailure { error ->
                AndroidAppLogger.warn(LogTag, "Failed to start hidden launcher standby", error)
            }
        }
    }
}

private const val ExtraHidden = "hidden"

private fun Context.hasOtherForegroundService(): Boolean {
    @Suppress("DEPRECATION")
    return getSystemService(ActivityManager::class.java)
        .getRunningServices(Int.MAX_VALUE)
        .any { info ->
            info.foreground &&
                info.service.packageName == packageName &&
                info.service.className != HiddenLauncherStandbyService::class.java.name
        }
}

private const val ChannelId = "hidden_launcher_standby"
private const val NotificationId = 3002
private const val LogTag = "HiddenLauncherStandby"
