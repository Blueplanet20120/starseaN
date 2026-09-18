// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package engine.root.runtime

import engine.proxy.ProxyEngineStatus
import engine.root.runtime.model.RootRuntimeMode
import engine.root.runtime.model.RootRuntimeOwner
import engine.root.runtime.model.RootRuntimeSnapshot
import engine.root.daemon.config.StarseadMode
import engine.root.daemon.config.StarseadOwner
import engine.root.daemon.control.StarseadControlResponse
import engine.root.daemon.control.StarseadPhase
import engine.root.daemon.control.StarseadResultCode
import engine.root.daemon.control.StarseadSnapshot
import system.ShellExecResult

internal fun ShellExecResult.controlResponseOrNull(): StarseadControlResponse? =
    runCatching { engine.root.daemon.control.StarseadControlCodec.decodeShellResponse(this) }.getOrNull()

internal fun StarseadControlResponse.boundSnapshot(): StarseadSnapshot? = when (result.code) {
    StarseadResultCode.Ok,
    StarseadResultCode.AlreadyRunning,
    StarseadResultCode.StopFailed,
    -> result.snapshot
    StarseadResultCode.NotRunning -> null
    else -> error(result.message ?: "starsead control request failed")
}

internal fun StarseadSnapshot.requireOwner(owner: StarseadOwner) {
    if (this.owner != owner) throw RootRuntimeConflictException(this)
}

internal fun StarseadSnapshot.rejectBound(owner: StarseadOwner): Nothing {
    requireOwner(owner)
    throw RootRuntimeBusyException(this)
}

internal fun StarseadSnapshot.requireRunning(owner: StarseadOwner, expectedMode: StarseadMode) {
    requireOwner(owner)
    check(phase == StarseadPhase.Running && mode == expectedMode)
}

internal enum class RootOrdinaryStartDisposition(
    val shutdownBeforeLaunch: Boolean,
) {
    Reuse(shutdownBeforeLaunch = false),
    Relaunch(shutdownBeforeLaunch = true),
}

internal fun StarseadSnapshot.ordinaryStartDisposition(
    owner: StarseadOwner,
    expectedMode: StarseadMode,
): RootOrdinaryStartDisposition {
    requireOwner(owner)
    if (phase == StarseadPhase.Running && mode == expectedMode) {
        return RootOrdinaryStartDisposition.Reuse
    }
    if (phase == StarseadPhase.Stopped) {
        return RootOrdinaryStartDisposition.Relaunch
    }
    rejectBound(owner)
}

internal fun StarseadControlResponse.preflightStart(
    owner: StarseadOwner,
    expectedMode: StarseadMode,
    explicitRestart: Boolean,
): StarseadSnapshot? {
    val snapshot = boundSnapshot() ?: return null
    snapshot.requireOwner(owner)
    if (explicitRestart) return null
    return when (snapshot.ordinaryStartDisposition(owner, expectedMode)) {
        RootOrdinaryStartDisposition.Reuse -> snapshot
        RootOrdinaryStartDisposition.Relaunch -> null
    }
}

internal fun StarseadSnapshot.toProxyEngineStatus(
    runMode: Int,
    expectedMode: StarseadMode,
): ProxyEngineStatus {
    val rootSnapshot = RootRuntimeSnapshot(
        owner = RootRuntimeOwner.entries.single { candidate -> candidate.wireValue == owner.wireValue },
        mode = RootRuntimeMode.entries.single { candidate -> candidate.wireValue == mode.wireValue },
        running = phase == StarseadPhase.Running,
    )
    return ProxyEngineStatus.fromRootSnapshot(
        localOwner = RootRuntimeOwner.StarseaN,
        runMode = runMode,
        snapshot = rootSnapshot,
    ).copy(running = owner == StarseadOwner.StarseaN && phase == StarseadPhase.Running && mode == expectedMode)
}

internal fun StarseadSnapshot.toStableProxyEngineStatus(
    runMode: Int,
    expectedMode: StarseadMode,
): ProxyEngineStatus? = when (phase) {
    StarseadPhase.Running,
    StarseadPhase.Stopped,
    StarseadPhase.Failed,
    -> toProxyEngineStatus(runMode, expectedMode)
    else -> null
}
