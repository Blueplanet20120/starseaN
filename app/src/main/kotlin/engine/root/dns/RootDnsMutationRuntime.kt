// SPDX-License-Identifier: GPL-3.0
package engine.root.dns

import android.content.Context
import app.AppState
import app.modes.supportsAppListEnhancement
import engine.root.daemon.StarseadClient
import engine.root.daemon.config.StarseadOwner
import engine.root.daemon.control.StarseadControlResponse
import engine.root.daemon.control.StarseadPhase
import engine.root.daemon.control.StarseadResultCode
import engine.root.mode.RootModeCatalog
import engine.root.publication.rootRuntimeLayout
import system.AndroidRootShellGateway

internal suspend fun <T> Context.withRootDnsMutation(appState: AppState, mutation: suspend () -> T): T =
    RootDnsMutation.run(
        usesDaemonDns = appState.proxyRunning && appState.runMode.supportsAppListEnhancement(),
        backendFactory = {
            val executable = rootRuntimeLayout().starseadPath
            val client = StarseadClient(AndroidRootShellGateway())
            val expectedMode = RootModeCatalog.require(appState.runMode).daemonMode
            fun StarseadControlResponse.state(): RootDnsEnhancementState {
                check(result.code == StarseadResultCode.Ok) { result.message ?: "DNS control operation failed" }
                val snapshot = requireNotNull(result.snapshot)
                check(snapshot.owner == StarseadOwner.StarseaN && snapshot.mode == expectedMode &&
                    snapshot.phase == StarseadPhase.Running) { "ROOT runtime changed during core mutation" }
                return snapshot.dnsEnhancement
            }
            object : RootDnsMutationBackend {
                override suspend fun status() = client.status(executable).state()
                override suspend fun suspendDns(token: RootDnsEnhancementState) = client.suspendDns(executable, token).state()
                override suspend fun resumeDns(token: RootDnsEnhancementState) = client.resumeDns(executable, token).state()
                override suspend fun abortDns(token: RootDnsEnhancementState) {
                    val response = client.abortDns(executable, token)
                    check(response.result.code == StarseadResultCode.Ok) {
                        response.result.message ?: "DNS mutation abort failed"
                    }
                }
            }
        },
        mutation = mutation,
    )
