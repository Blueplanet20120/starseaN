// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package engine.vpn

import app.AppState
import engine.proxy.LocalProxyLoopbackAddress
import engine.proxy.LocalProxyOptions
import engine.proxy.availablePort
import engine.xray.XrayProtocols
import engine.xray.XrayTags
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.put

internal data class VpnAppendHttpProxyOptions(
    val enabled: Boolean,
    val port: Int,
) {
    companion object {
        val Disabled = VpnAppendHttpProxyOptions(
            enabled = false,
            port = 0,
        )
    }
}

internal fun AppState.toVpnAppendHttpProxyOptions(localProxyOptions: LocalProxyOptions): VpnAppendHttpProxyOptions {
    if (!enableVpnAppendHttpProxy) {
        return VpnAppendHttpProxyOptions.Disabled
    }
    return VpnAppendHttpProxyOptions(
        enabled = true,
        port = availablePort(
            listenAddress = LocalProxyLoopbackAddress,
            excludedPorts = setOf(localProxyOptions.port),
        ) ?: fallbackAppendHttpProxyPort(localProxyOptions.port),
    )
}

internal fun buildVpnAppendHttpInbound(options: VpnAppendHttpProxyOptions): JsonObject {
    return buildJsonObject {
        put("tag", XrayTags.VPN_APPEND_HTTP_INBOUND)
        put("listen", LocalProxyLoopbackAddress)
        put("port", options.port)
        put("protocol", XrayProtocols.HTTP)
        put(
            "settings",
            buildJsonObject {
                put("allowTransparent", false)
                put("userLevel", 0)
            },
        )
    }
}

private fun fallbackAppendHttpProxyPort(localProxyPort: Int): Int {
    return if (VpnDefaults.VPN_APPEND_HTTP_PROXY_FALLBACK_PORT == localProxyPort) {
        VpnDefaults.VPN_APPEND_HTTP_PROXY_FALLBACK_PORT + 1
    } else {
        VpnDefaults.VPN_APPEND_HTTP_PROXY_FALLBACK_PORT
    }
}
