// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package system

import android.app.ActivityManager
import android.content.Context

internal fun Context.setRecentTasksHidden(hidden: Boolean) {
    getSystemService(ActivityManager::class.java).appTasks.forEach { task ->
        task.setExcludeFromRecents(hidden)
    }
}
