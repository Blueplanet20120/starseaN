// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package app

import android.app.ActivityOptions
import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.os.Build
import com.topjohnwu.superuser.Shell
import data.AppSettingsPreferences
import features.logs.AndroidAppLogger
import system.AndroidRootShell
import utils.shellQuote

class SecretCodeReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action !in SecretCodeActions) return
        val appContext = context.applicationContext
        val dialed = intent.dialedSecretCode()
        val expected = AppSettingsPreferences(appContext).load().hideLauncherSecretCode
        if (dialed != expected) return
        val pending = goAsync()
        Thread {
            try {
                if (!openWithRoot(appContext)) {
                    openWithActivity(appContext)
                }
            } finally {
                pending.finish()
            }
        }.start()
    }
}

private fun openWithRoot(context: Context): Boolean {
    AndroidRootShell.configure()
    val rooted = runCatching { Shell.getShell().isRoot }.getOrDefault(false)
    if (!rooted) return false
    val component = ComponentName(context, MainActivity::class.java).flattenToShortString()
    val result = runCatching {
        Shell.cmd(
            "am start -n ${component.shellQuote()} -a ${Intent.ACTION_MAIN.shellQuote()} -f $ActivityFlags",
        ).exec()
    }.onFailure { error ->
        AndroidAppLogger.warn(LogTag, "Failed to open app from secret code with root", error)
    }.getOrNull()
    if (result == null || !result.isSuccess) {
        AndroidAppLogger.warn(LogTag, "Root launch from secret code failed code=${result?.code}")
        return false
    }
    return true
}

private fun openWithActivity(context: Context) {
    val launch = Intent(context, MainActivity::class.java)
        .setAction(Intent.ACTION_MAIN)
        .addCategory(Intent.CATEGORY_LAUNCHER)
        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
    runCatching {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            val options = ActivityOptions.makeBasic()
            options.pendingIntentBackgroundActivityStartMode =
                ActivityOptions.MODE_BACKGROUND_ACTIVITY_START_ALLOWED
            context.startActivity(launch, options.toBundle())
        } else {
            context.startActivity(launch)
        }
    }.onFailure { error ->
        AndroidAppLogger.warn(LogTag, "Failed to open app from secret code", error)
    }
}

private fun Intent.dialedSecretCode(): String {
    val data = data ?: return ""
    val raw = data.host?.takeIf { it.isNotBlank() }
        ?: data.schemeSpecificPart
    return raw.filter(Char::isDigit)
}

private val SecretCodeActions = setOf(
    "android.provider.Telephony.SECRET_CODE",
    "android.telephony.action.SECRET_CODE",
)

private const val ActivityFlags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP
private const val LogTag = "SecretCode"
