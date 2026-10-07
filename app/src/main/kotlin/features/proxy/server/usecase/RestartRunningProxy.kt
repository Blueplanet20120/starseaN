// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package features.proxy.server.usecase

import app.AppState
import features.routing.model.RouteRule
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import ui.feedback.AndroidToastTipNotifier

internal fun AppState.proxyConnectionRevision(): List<Any?> {
    return listOf(
        runMode,
        selectedProxyServerId,
        enableVpnLocalDns,
        enableFakeDns,
        enableResolveProxyServerDomain,
        proxyDns,
        directDns,
        directDnsDomains,
        enableDirectDnsForProxyServerDomains,
        dnsHosts,
        enableSniffing,
        enableSniffingRouteOnly,
        enableMux,
        muxConcurrency,
        muxXudpConcurrency,
        muxXudpProxyUdp443,
        enableFragment,
        fragmentPackets,
        fragmentLength,
        fragmentInterval,
        enableIpv6,
        enableIpv6Prefer,
        coreLogLevel,
        enableAccessLog,
        tunMtu,
        tunVpnDns,
        tunIpv4Cidr,
        tunIpv6Cidr,
        enableVpnAppendHttpProxy,
        enableVpnHevTun,
        transparentProxyPort,
        socks5ProxyPort,
        bpf2SocksBridgePort,
        localProxyPort,
        enableDynamicLocalProxyPort,
        localProxyListenAllInterfaces,
        localProxyUsername,
        localProxyPassword,
        enableRootEbpfRules,
        enableRootEbpfDirectCidrBypass,
        enableRootIpv6Disabler,
        externalInterfaces,
        ignoredInterfaces,
        privateAddressCidrs,
        proxyAppListMode,
        proxyAppListSelectedApps,
        routeDomainStrategy,
        defaultRouteOutboundTag,
        routeRules.map(RouteRule::id),
        routeRules,
    )
}

internal fun CoroutineScope.restartProxyIfConnectionChanged(
    before: AppState,
    after: AppState,
    proxyServiceUseCase: ProxyServiceUseCase,
    updateAppState: ((AppState) -> AppState) -> Unit,
    tipNotifier: AndroidToastTipNotifier,
    restartedMessage: String,
    heldMessage: String,
    missingServerMessage: String,
    failedMessage: String,
) {
    if (!before.proxyRunning) return
    if (before.proxyConnectionRevision() == after.proxyConnectionRevision()) return
    val selectedServer = after.proxyServers.firstOrNull { server ->
        server.id == after.selectedProxyServerId
    }
    launch {
        when (
            val result = proxyServiceUseCase.restart(
                state = after,
                selectedServer = selectedServer,
            )
        ) {
            is ProxyServiceResult.Success -> {
                updateAppState { state ->
                    state.copy(
                        proxyRunning = result.proxyRunning,
                        localProxyPort = result.appState?.localProxyPort ?: state.localProxyPort,
                    )
                }
                tipNotifier.show(
                    if (result.heldByServiceControl) heldMessage else restartedMessage,
                )
            }

            ProxyServiceResult.MissingServer -> tipNotifier.show(missingServerMessage)

            is ProxyServiceResult.Failed -> {
                updateAppState { state -> state.copy(proxyRunning = false) }
                tipNotifier.showError(result.error, failedMessage)
            }
        }
    }
}
