// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package engine.root

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import app.modes.isRootRunMode
import data.AndroidAppStateStore
import engine.proxy.AndroidProxyEngine
import engine.proxy.ProxyEngineStartRequest
import features.logs.AndroidAppLogger
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import system.AndroidRootShellGateway
import kotlin.coroutines.cancellation.CancellationException

class RootPackageReplacedReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_MY_PACKAGE_REPLACED) return
        val pendingResult = goAsync()
        val appContext = context.applicationContext
        scope.launch {
            try {
                restoreResidentSupervisor(appContext)
            } catch (error: Throwable) {
                if (error is CancellationException) throw error
                AndroidAppLogger.error(LogTag, "Failed to restore the root supervisor after update", error)
            } finally {
                pendingResult.finish()
            }
        }
    }

    companion object {
        private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    }
}

private suspend fun restoreResidentSupervisor(context: Context) {
    val stateStore = AndroidAppStateStore.get(context)
    val state = stateStore.state.value
    if (!state.runMode.isRootRunMode()) return
    if (!state.enableRootBootScript && !state.serviceControl.enabled) return
    val rootAccess = AndroidRootShellGateway()
    if (!rootAccess.hasRootAccess()) {
        AndroidAppLogger.warn(LogTag, "Skipped root supervisor restore because root access is unavailable")
        return
    }
    val selectedServer = state.proxyServers.firstOrNull { server -> server.id == state.selectedProxyServerId }
    if (selectedServer == null) {
        AndroidAppLogger.warn(LogTag, "Skipped root supervisor restore because no proxy server is selected")
        return
    }
    val engine = AndroidProxyEngine(
        context = context,
        rootAccess = rootAccess,
        requestVpnPermission = { false },
    )
    val status = engine.restart(ProxyEngineStartRequest(state, selectedServer))
    stateStore.update { current ->
        current.copy(
            proxyRunning = status.running,
            localProxyPort = status.appState?.localProxyPort ?: current.localProxyPort,
        )
    }
    AndroidAppLogger.info(
        LogTag,
        "Restored root supervisor after update: running=${status.running} held=${status.heldByServiceControl}",
    )
}

private const val LogTag = "RootPackageReplaced"
