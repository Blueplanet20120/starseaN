// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package app

import android.app.ActivityOptions
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Build
import data.AppSettingsPreferences
import features.logs.AndroidAppLogger

class SecretCodeReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action !in SecretCodeActions) return
        val dialed = intent.dialedSecretCode()
        val expected = AppSettingsPreferences(context.applicationContext)
            .load()
            .hideLauncherSecretCode
        if (dialed != expected) return
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

private const val LogTag = "SecretCode"
