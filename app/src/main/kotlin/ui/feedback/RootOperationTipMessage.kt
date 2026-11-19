// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package ui.feedback

import android.content.Context
import app.R
import data.AppSettingsPreferences
import engine.root.runtime.RootOperationBlockedException
import engine.root.runtime.RootOperationResult
import engine.root.runtime.RootPrivateDnsException
import engine.root.runtime.model.RootRuntimeOwner
import features.settings.locale.localizedAppContext

internal fun RootRuntimeOwner.productName(): String = when (this) {
    RootRuntimeOwner.StarseaN -> "starseaN"
    RootRuntimeOwner.AsteriskMeta -> "AsteriskMETA"
    RootRuntimeOwner.AsteriskBox -> "AsteriskBOX"
}

internal fun Throwable.rootOperationTipMessageOrNull(context: Context): String? {
    if (this is RootPrivateDnsException) {
        val localized = context.applicationContext.let { appContext ->
            appContext.localizedAppContext(AppSettingsPreferences(appContext).load().languageMode)
        }
        return localized.getString(R.string.error_app_list_enhancement_private_dns)
    }
    val result = (this as? RootOperationBlockedException)?.result
    return if (result is RootOperationResult.ForeignOwnerConflict) {
        context.getString(R.string.root_foreign_owner_conflict, result.owner.productName())
    } else {
        null
    }
}
