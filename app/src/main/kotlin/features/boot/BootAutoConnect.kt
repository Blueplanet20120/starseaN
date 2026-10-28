// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package features.boot

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.VpnService
import androidx.core.content.ContextCompat
import app.MainActivity
import app.R
import app.StarseaApplication
import app.modes.RunModeVpnService
import engine.proxy.ProxyEngineStartRequest
import engine.vpn.StarseaVpnServiceIntents
import features.logs.AndroidAppLogger
import kotlin.coroutines.cancellation.CancellationException

/**
 * Entry points for the VPN Service that run without a visible activity.
 *
 * [AutoConnectBootReceiver] only hands the service to the foreground; the service itself
 * compiles the configuration through the regular proxy engine, so the boot path cannot drift
 * away from an ordinary start. When the VPN consent is missing the receiver cannot show the
 * system dialog (background activity launches are blocked), so it posts a notification that
 * reuses the activity consent flow through [connectAfterConsent].
 */
internal object BootAutoConnect {
    const val ActionConnectAfterConsent = "app.action.CONNECT_AFTER_CONSENT"

    private const val LogTag = "BootAutoConnect"

    fun setComponentEnabled(context: Context, enabled: Boolean) {
        val component = ComponentName(context, AutoConnectBootReceiver::class.java)
        val manager = context.packageManager
        val desired = if (enabled) {
            PackageManager.COMPONENT_ENABLED_STATE_ENABLED
        } else {
            PackageManager.COMPONENT_ENABLED_STATE_DISABLED
        }
        if (manager.getComponentEnabledSetting(component) == desired) return
        runCatching {
            manager.setComponentEnabledSetting(component, desired, PackageManager.DONT_KILL_APP)
        }.onFailure { error ->
            AndroidAppLogger.warn(LogTag, "Failed to update the auto connect receiver state", error)
        }
    }

    fun needsVpnPermission(context: Context): Boolean = VpnService.prepare(context) != null

    fun startService(context: Context) {
        ContextCompat.startForegroundService(
            context,
            StarseaVpnServiceIntents.resumeIntent(context),
        )
    }

    fun consentIntent(context: Context): Intent {
        return Intent(context, MainActivity::class.java)
            .setAction(ActionConnectAfterConsent)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    }

    suspend fun connectAfterConsent(
        context: Context,
        requestVpnPermission: suspend (Intent) -> Boolean,
    ): Result<Boolean> {
        val application = context.applicationContext as? StarseaApplication
            ?: return Result.success(false)
        val state = application.stateStore.state.value
        if (state.runMode != RunModeVpnService) return Result.success(false)
        return runCatching {
            val prepareIntent = VpnService.prepare(context)
            if (prepareIntent != null && !requestVpnPermission(prepareIntent)) {
                return@runCatching false
            }
            val server = checkNotNull(
                state.proxyServers.firstOrNull { server -> server.id == state.selectedProxyServerId },
            ) {
                application.getString(R.string.proxy_server_list_select_first)
            }
            val status = application.proxyEngine.start(ProxyEngineStartRequest(state, server))
            application.stateStore.update { current ->
                current.copy(
                    proxyRunning = status.running,
                    localProxyPort = status.appState?.localProxyPort ?: current.localProxyPort,
                )
            }
            status.running
        }.onFailure { error ->
            if (error is CancellationException) throw error
            AndroidAppLogger.warn(LogTag, "Failed to connect after granting the VPN permission", error)
            application.stateStore.update { current -> current.copy(proxyRunning = false) }
        }
    }
}
