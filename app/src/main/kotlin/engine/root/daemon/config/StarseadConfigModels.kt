// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package engine.root.daemon.config

import android.os.Build
import app.ServiceControlKeyguard
import app.ServiceControlSettings
import app.supportsKeyguardControl
import features.settings.servicecontrol.normalizeServiceControlSettings

internal enum class StarseadOwner(val wireValue: String) {
    StarseaN("starsean"),
    AsteriskBox("asteriskbox"),
    AsteriskMeta("asteriskmeta"),
}

internal enum class StarseadCoreType(val wireValue: String) {
    Xray("xray"),
    SingBox("sing-box"),
    Mihomo("mihomo"),
}

internal enum class StarseadMode(val wireValue: String) {
    Tproxy("tproxy"),
    Tun("tun"),
    Tun2Socks("tun2socks"),
    Bpf2Socks("bpf2socks"),
    Ebpf("ebpf"),
    ;

    companion object {
        fun fromWire(value: String): StarseadMode = entries.firstOrNull { it.wireValue == value }
            ?: throw IllegalArgumentException("Unknown starsead mode")
    }
}

internal enum class StarseadAppPolicyMode(val wireValue: String) {
    Global("global"),
    Blacklist("blacklist"),
    Whitelist("whitelist"),
}

internal data class StarseadConfig(
    val owner: StarseadOwner,
    val coreType: StarseadCoreType,
    val coreExecutablePath: String,
    val coreConfigPath: String,
    val statePath: String,
    val logPath: String,
    val mode: StarseadMode,
    val core: StarseadCoreConfig,
    val network: StarseadNetworkConfig,
    val modeOptions: StarseadModeOptions,
    val matcher: StarseadMatcher?,
    val helper: StarseadHelper?,
    val serviceControl: StarseadServiceControlConfig,
)

internal data class StarseadServiceControlConfig(
    val enabled: Boolean,
    val schedule: StarseadScheduleControl,
    val wifi: StarseadWifiControl,
    val keyguard: ServiceControlKeyguard = ServiceControlKeyguard(),
)

internal data class StarseadScheduleControl(
    val enabled: Boolean,
    val startCron: String,
    val stopCron: String,
)

internal data class StarseadWifiControl(
    val enabled: Boolean,
    val connectStart: StarseadWifiRule,
    val connectStop: StarseadWifiRule,
    val disconnectStart: StarseadWifiRule,
    val disconnectStop: StarseadWifiRule,
)

internal data class StarseadWifiRule(
    val enabled: Boolean,
    val ssids: List<String>,
    val bssids: List<String>,
)

internal fun ServiceControlSettings.toStarseadServiceControlConfig(): StarseadServiceControlConfig {
    val value = normalizeServiceControlSettings(this)
    return StarseadServiceControlConfig(
        enabled = value.enabled,
        keyguard = value.keyguard.copy(enabled = value.keyguard.enabled && supportsKeyguardControl(Build.VERSION.SDK_INT)),
        schedule = StarseadScheduleControl(
            enabled = value.schedule.enabled,
            startCron = value.schedule.startCron,
            stopCron = value.schedule.stopCron,
        ),
        wifi = StarseadWifiControl(
            enabled = value.wifi.enabled,
            connectStart = value.wifi.connectStart.toStarseadWifiRule(),
            connectStop = value.wifi.connectStop.toStarseadWifiRule(),
            disconnectStart = value.wifi.disconnectStart.toStarseadWifiRule(),
            disconnectStop = value.wifi.disconnectStop.toStarseadWifiRule(),
        ),
    )
}

private fun app.ServiceControlWifiRule.toStarseadWifiRule(): StarseadWifiRule =
    StarseadWifiRule(enabled = enabled, ssids = ssids, bssids = bssids)

internal data class StarseadCoreConfig(
    val workingDirectory: String,
    val readinessTimeoutMilliseconds: Int,
    val ageSecretKey: String?,
)

internal data class StarseadNetworkConfig(
    val enableIpv6: Boolean,
    val disableSystemIpv6: Boolean,
    val enableLocalDns: Boolean,
    val enableFakeDns: Boolean,
    val fakeDnsIpv4Pool: String?,
    val ignoredInterfaces: List<String>,
    val virtualInterfaces: List<String>,
    val hotspotInterfacePrefixes: List<String>,
    val proxyPrivateCidrs: List<String>,
    val bypassPrivateCidrs: List<String>,
    val appPolicy: StarseadAppPolicy,
)

internal data class StarseadAppPolicy(
    val mode: StarseadAppPolicyMode,
    val uids: List<Int>,
    val bypassUids: List<Int>,
    val directCidrPathV4: String?,
    val directCidrPathV6: String?,
)

internal data class StarseadModeOptions(
    val transparentPort: Int?,
    val tunnelName: String?,
)

internal data class StarseadMatcher(
    val executablePath: String,
)

internal sealed interface StarseadHelper

internal data class StarseadHevSocks5TunnelHelper(
    val executablePath: String,
    val socksHost: String,
    val socksPort: Int,
    val tunnelName: String,
    val mtu: Int,
    val ipv4Address: String,
    val ipv6Address: String?,
    val multiQueue: Boolean,
    val tcpFastOpen: Boolean,
    val tcpReadWriteTimeoutMilliseconds: Int = 300000,
    val udpReadWriteTimeoutMilliseconds: Int = 60000,
) : StarseadHelper

internal data class StarseadBpf2SocksHelper(
    val executablePath: String,
    val bridgeListenAddress: String,
    val bridgePort: Int,
    val socksHost: String,
    val socksPort: Int,
    val workerCount: Int = 0,
    val tcpBufferSize: Int = 65536,
    val maxTcpSessions: Int = 4096,
    val tcpConnectTimeoutMilliseconds: Int = 10000,
    val tcpIdleTimeoutMilliseconds: Int = 300000,
    val udpSocketBufferSize: Int = 524288,
    val udpBatchSize: Int = 32,
    val maxUdpSessions: Int = 4096,
    val maxUdpBindings: Int = 16384,
    val udpIdleTimeoutSeconds: Int = 60,
    val maxUdpPendingBytes: Int = 64 * 1024 * 1024,
    val dnsTransactionTimeoutMilliseconds: Int = 60000,
) : StarseadHelper
