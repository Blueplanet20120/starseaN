// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package app.effects

import android.content.ComponentName
import android.content.Context
import android.content.pm.PackageManager
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import app.AppState
import app.MainActivity
import app.modes.ColorModeThemeDark
import app.modes.ColorModeThemeSystem
import data.AndroidAppStateStore
import features.logs.AndroidAppLogger
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.withContext

@Composable
internal fun LauncherIconSynchronizer(
    context: Context,
    stateStore: AndroidAppStateStore,
) {
    val appContext = context.applicationContext
    LaunchedEffect(appContext, stateStore) {
        stateStore.state
            .map { state -> state.usesMonetLauncherIcon to state.hideLauncherIcon }
            .distinctUntilChanged()
            .collect { (useMonetIcon, hideLauncherIcon) ->
                runCatching {
                    withContext(Dispatchers.IO) {
                        appContext.setLauncherIcon(useMonetIcon, hideLauncherIcon)
                    }
                }.onFailure { error ->
                    AndroidAppLogger.warn(
                        LogTag,
                        "Failed to synchronize launcher icon",
                        error,
                    )
                }
            }
    }
}

internal fun syncLauncherIcon(context: Context, state: AppState) {
    context.applicationContext.setLauncherIcon(state.usesMonetLauncherIcon, state.hideLauncherIcon)
}

private val AppState.usesMonetLauncherIcon: Boolean
    get() = colorMode in ColorModeThemeSystem..ColorModeThemeDark

private fun Context.setLauncherIcon(useMonetIcon: Boolean, hidden: Boolean) {
    val packageManager = packageManager
    val launcherPackageName = MainActivity::class.java.name.substringBeforeLast('.')
    val defaultLauncher = ComponentName(packageName, "$launcherPackageName.DefaultLauncherActivity")
    val monetLauncher = ComponentName(packageName, "$launcherPackageName.MonetLauncherActivity")
    if (hidden) {
        packageManager.setSyntheticDetailsIconEnabled(packageName, false)
        packageManager.setLauncherEnabled(defaultLauncher, false)
        packageManager.setLauncherEnabled(monetLauncher, false)
        return
    }
    val enabledLauncher = if (useMonetIcon) monetLauncher else defaultLauncher
    val disabledLauncher = if (useMonetIcon) defaultLauncher else monetLauncher
    packageManager.setLauncherEnabled(enabledLauncher, true)
    packageManager.setLauncherEnabled(disabledLauncher, false)
    packageManager.setSyntheticDetailsIconEnabled(packageName, true)
}

private fun PackageManager.setLauncherEnabled(component: ComponentName, enabled: Boolean) {
    setComponentEnabledSetting(
        component,
        if (enabled) {
            PackageManager.COMPONENT_ENABLED_STATE_ENABLED
        } else {
            PackageManager.COMPONENT_ENABLED_STATE_DISABLED
        },
        PackageManager.DONT_KILL_APP or ComponentUpdateSynchronous,
    )
}

private fun PackageManager.setSyntheticDetailsIconEnabled(packageName: String, enabled: Boolean) {
    val state = if (enabled) {
        PackageManager.COMPONENT_ENABLED_STATE_DEFAULT
    } else {
        PackageManager.COMPONENT_ENABLED_STATE_DISABLED
    }
    runCatching {
        setComponentEnabledSetting(
            ComponentName(packageName, SyntheticDetailsClass),
            state,
            PackageManager.DONT_KILL_APP or ComponentUpdateSynchronous,
        )
    }.onFailure { error ->
        if (error !is IllegalArgumentException) {
            AndroidAppLogger.warn(LogTag, "Failed to update synthetic launcher icon", error)
        }
    }
    runCatching {
        val method = javaClass.methods.firstOrNull { candidate ->
            candidate.name == "setSyntheticAppDetailsActivityEnabled" &&
                candidate.parameterTypes.contentEquals(
                    arrayOf(String::class.java, java.lang.Boolean.TYPE),
                )
        } ?: return@runCatching
        method.invoke(this, packageName, enabled)
    }.onFailure { error ->
        val cause = (error as? java.lang.reflect.InvocationTargetException)?.cause ?: error
        if (cause !is NoSuchMethodException) {
            AndroidAppLogger.warn(LogTag, "Failed to update synthetic launcher setting", cause)
        }
    }
}

private const val SyntheticDetailsClass = "android.app.AppDetailsActivity"
private const val ComponentUpdateSynchronous = 2
private const val LogTag = "LauncherIconSync"
