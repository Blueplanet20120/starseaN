// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package engine.root.config

import app.modes.ProxyAppListModeBlacklist
import app.modes.ProxyAppListModeGlobal
import app.modes.ProxyAppListModeWhitelist
import engine.root.daemon.config.StarseadAppPolicy
import engine.root.daemon.config.StarseadAppPolicyMode
import engine.root.daemon.config.StarseadConfig
import engine.root.daemon.config.StarseadCoreConfig
import engine.root.daemon.config.StarseadCoreType
import engine.root.daemon.config.StarseadHelper
import engine.root.daemon.config.StarseadMatcher
import engine.root.daemon.config.StarseadMode
import engine.root.daemon.config.StarseadModeOptions
import engine.root.daemon.config.StarseadNetworkConfig
import engine.root.daemon.config.StarseadNgConfigFactory
import engine.root.daemon.config.StarseadOwner
import engine.root.daemon.config.toStarseadServiceControlConfig

private val RootStartConfig.disableSystemIpv6: Boolean
    get() = !enableIpv6 && enableRootIpv6Disabler

internal fun RootStartConfig.buildStarseadConfig(
    mode: StarseadMode,
    iptablesConfig: RootIptablesConfig,
    virtualInterfaces: List<String>,
    modeOptions: StarseadModeOptions,
    helper: StarseadHelper? = null,
): StarseadConfig {
    StarseadNgConfigFactory.requireRunnableMode(mode)
    val matcher = if (iptablesConfig.enableEbpfRules && mode != StarseadMode.Bpf2Socks) {
        StarseadMatcher(runtimePaths.matcherExecutablePath)
    } else {
        null
    }
    val useDirectCidrs =
        iptablesConfig.enableEbpfDirectCidrBypass && (matcher != null || mode == StarseadMode.Bpf2Socks)
    return StarseadConfig(
        owner = StarseadOwner.StarseaN,
        coreType = StarseadCoreType.Xray,
        coreExecutablePath = runtimePaths.coreExecutablePath,
        coreConfigPath = runtimePaths.coreConfigPath,
        statePath = runtimePaths.statePath,
        logPath = runtimePaths.logPath,
        mode = mode,
        core = StarseadCoreConfig(
            workingDirectory = runtimePaths.workingDirectory,
            readinessTimeoutMilliseconds = 5000,
            ageSecretKey = null,
        ),
        network = StarseadNetworkConfig(
            enableIpv6 = enableIpv6,
            disableSystemIpv6 = disableSystemIpv6,
            enableLocalDns = enableLocalDns,
            enableFakeDns = enableFakeDns,
            fakeDnsIpv4Pool = "198.18.0.0/15".takeIf { enableFakeDns },
            ignoredInterfaces = iptablesConfig.ignoredInterfaces.distinct(),
            virtualInterfaces = virtualInterfaces.distinct(),
            hotspotInterfacePrefixes = iptablesConfig.externalInterfacePrefixes.distinct(),
            proxyPrivateCidrs = (
                iptablesConfig.proxyPrivateIpv4Cidrs +
                    iptablesConfig.proxyPrivateIpv6Cidrs.takeIf { enableIpv6 }.orEmpty()
                ).distinct(),
            bypassPrivateCidrs = (
                iptablesConfig.bypassPrivateIpv4Cidrs +
                    iptablesConfig.bypassPrivateIpv6Cidrs.takeIf { enableIpv6 }.orEmpty()
                ).distinct(),
            appPolicy = StarseadAppPolicy(
                mode = when (iptablesConfig.proxyAppListMode) {
                    ProxyAppListModeBlacklist -> StarseadAppPolicyMode.Blacklist
                    ProxyAppListModeWhitelist -> StarseadAppPolicyMode.Whitelist
                    ProxyAppListModeGlobal -> StarseadAppPolicyMode.Global
                    else -> error("Unsupported application policy mode")
                },
                uids = iptablesConfig.proxyApplicationUids.distinct().sorted()
                    .takeUnless { iptablesConfig.proxyAppListMode == ProxyAppListModeGlobal }
                    .orEmpty(),
                bypassUids = iptablesConfig.forcedBypassUids.distinct().sorted(),
                directCidrPathV4 = directCidrIpv4Path.takeIf { useDirectCidrs },
                directCidrPathV6 = directCidrIpv6Path.takeIf { useDirectCidrs },
            ),
        ),
        modeOptions = modeOptions,
        matcher = matcher,
        helper = helper,
        serviceControl = serviceControl.toStarseadServiceControlConfig(),
    )
}
