// SPDX-License-Identifier: GPL-3.0
package engine.root.dns

import engine.root.daemon.config.StarseadDnsEnhancementConfig
import engine.root.daemon.config.StarseadMode
import java.nio.ByteBuffer
import java.security.MessageDigest

// Pure configuration planning: VPN never enters the ROOT compiler and core-managed
// modes produce no request. Capability is checked again by the listener-owning daemon.
internal fun rootDnsEnhancementConfig(
    requested: Boolean,
    mode: StarseadMode,
    enableLocalDns: Boolean,
    coreConfig: ByteArray,
): StarseadDnsEnhancementConfig? {
    if (!requested || !enableLocalDns || mode == StarseadMode.Tun || mode == StarseadMode.Ebpf) return null
    val digest = MessageDigest.getInstance("SHA-256").digest(coreConfig)
    val generation = (ByteBuffer.wrap(digest).long and Long.MAX_VALUE).coerceAtLeast(1L)
    return StarseadDnsEnhancementConfig(coreDnsGeneration = generation)
}
