// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package features.resources.runtime

import android.content.Context
import app.ProjectInfo

internal class AndroidXrayCoreVersionStore(
    context: Context,
) {
    private val preferences = context.applicationContext.getSharedPreferences(
        PreferencesName,
        Context.MODE_PRIVATE,
    )

    fun installedVersion(): String {
        return preferences.getString(KeyInstalledVersion, null)
            ?.trim()
            ?.takeIf(String::isNotEmpty)
            ?: ProjectInfo.ANDROID_LIB_XRAY_LITE_VERSION
    }

    fun setInstalledVersion(version: String) {
        val normalized = version.trim()
        if (normalized.isEmpty()) return
        preferences.edit().putString(KeyInstalledVersion, normalized).apply()
    }

    fun updatedAtMillis(): Long {
        return preferences.getLong(KeyUpdatedAtMillis, 0L)
    }

    fun touchUpdatedAt(atMillis: Long = System.currentTimeMillis()) {
        if (atMillis <= 0L) return
        preferences.edit().putLong(KeyUpdatedAtMillis, atMillis).apply()
    }

    fun clearInstalledVersion() {
        preferences.edit()
            .remove(KeyInstalledVersion)
            .remove(KeyUpdatedAtMillis)
            .apply()
    }
}

private const val PreferencesName = "xray_core_version"
private const val KeyInstalledVersion = "installed_version"
private const val KeyUpdatedAtMillis = "updated_at_millis"
