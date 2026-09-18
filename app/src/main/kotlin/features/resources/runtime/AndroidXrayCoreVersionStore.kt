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
            ?: ProjectInfo.XRAY_CORE_VERSION
    }

    fun setInstalledVersion(version: String) {
        val normalized = version.trim()
        if (normalized.isEmpty()) return
        preferences.edit().putString(KeyInstalledVersion, normalized).apply()
    }

    fun clearInstalledVersion() {
        preferences.edit().remove(KeyInstalledVersion).apply()
    }
}

private const val PreferencesName = "xray_core_version"
private const val KeyInstalledVersion = "installed_version"
