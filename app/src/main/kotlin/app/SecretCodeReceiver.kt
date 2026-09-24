// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package app

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.provider.Telephony
import data.AppSettingsPreferences

class SecretCodeReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Telephony.Sms.Intents.SECRET_CODE_ACTION) return
        val dialed = intent.data?.host?.trim().orEmpty()
        val expected = AppSettingsPreferences(context.applicationContext)
            .load()
            .hideLauncherSecretCode
        if (dialed != expected) return
        context.startActivity(
            Intent(context, MainActivity::class.java).addFlags(
                Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP,
            ),
        )
    }
}
