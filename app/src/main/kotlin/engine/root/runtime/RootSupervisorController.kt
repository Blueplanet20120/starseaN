// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package engine.root.runtime

import android.content.Context
import engine.proxy.ProxyEngineStatus
import engine.root.config.RootStartConfig
import engine.root.daemon.StarseadClient
import engine.root.daemon.config.StarseadConfig
import engine.root.daemon.config.StarseadConfigEncoder
import engine.root.daemon.config.StarseadMode
import engine.root.daemon.config.StarseadOwner
import engine.root.daemon.control.StarseadControlCodec
import engine.root.daemon.control.StarseadControlResponse
import engine.root.daemon.control.StarseadPhase
import engine.root.daemon.control.StarseadResultCode
import engine.root.daemon.control.StarseadSnapshot
import engine.root.publication.RootBootConfigWriter
import engine.root.publication.RootBootPublicationCommand
import engine.root.publication.RootPublicationBundle
import engine.root.publication.RootPublicationCommand
import engine.root.publication.RootPublicationWriter
import engine.root.publication.RootPublicationLaunchMode
import engine.root.publication.RootServiceLogCleanupWarningPrefix
import engine.root.publication.prepareRootPublicationDirectories
import engine.root.publication.rootRuntimeLayout
import features.logs.AndroidAppLogger
import features.logs.clearServiceLogRepositories
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import system.RootShellGateway
import system.ShellExecOptions
import system.ShellExecResult
import kotlin.time.Duration.Companion.milliseconds

internal class RootSupervisorController(
    context: Context,
    private val shell: RootShellGateway,
) {
    private val appContext = context.applicationContext
    private val runtimeLayout = appContext.rootRuntimeLayout()
    private val client = StarseadClient(shell)
    suspend fun status(): StarseadControlResponse = client.status(runtimeLayout.starseadPath)

    fun observeStatus(): Flow<StarseadSnapshot> = client.observeStatus(runtimeLayout.starseadPath)
        .onEach { snapshot -> observeRunningFailure(snapshot) }

    suspend fun preflightStart(expectedMode: StarseadMode, explicitRestart: Boolean): StarseadSnapshot? {
        return status().preflightStart(StarseadOwner.StarseaN, expectedMode, explicitRestart)
            ?.also { snapshot -> observeRunningFailure(snapshot, explicitRootAction = true) }
    }

    suspend fun ownsRuntime(): Boolean = status().boundSnapshot()?.owner == StarseadOwner.StarseaN

    suspend fun proxyStatus(runMode: Int, expectedMode: StarseadMode): ProxyEngineStatus {
        val snapshot = status().boundSnapshot() ?: return ProxyEngineStatus(running = false, runMode = runMode)
        observeRunningFailure(snapshot)
        return snapshot.toProxyEngineStatus(runMode, expectedMode)
    }

    private suspend fun observeRunningFailure(snapshot: StarseadSnapshot, explicitRootAction: Boolean = false) {
        if (snapshot.owner == StarseadOwner.StarseaN && snapshot.phase == StarseadPhase.Running) {
            RootFailureWatcher.ensureStarted(appContext, shell, runtimeLayout, explicitRootAction)
        }
    }

    fun proxyStatus(snapshot: StarseadSnapshot, runMode: Int, expectedMode: StarseadMode): ProxyEngineStatus =
        snapshot.toProxyEngineStatus(runMode, expectedMode)

    fun requireRunning(snapshot: StarseadSnapshot, expectedMode: StarseadMode) {
        snapshot.requireRunning(StarseadOwner.StarseaN, expectedMode)
    }

    suspend fun start(
        root: RootStartConfig,
        config: StarseadConfig,
    ): StarseadSnapshot {
        RootFailureWatcher.beginAttempt()
        status().boundSnapshot()?.let { snapshot ->
            val disposition = snapshot.ordinaryStartDisposition(StarseadOwner.StarseaN, config.mode)
            if (disposition == RootOrdinaryStartDisposition.Reuse) {
                observeRunningFailure(snapshot, explicitRootAction = true)
                return snapshot
            }
            if (disposition.shutdownBeforeLaunch) {
                shutdownOwn()
            }
            return launch(
                root = root,
                config = config,
                restartExpectedOwner = snapshot.owner,
                launchMode = RootPublicationLaunchMode.Service,
            )
        }

        return launch(root, config, restartExpectedOwner = null, RootPublicationLaunchMode.Service)
    }

    suspend fun restart(
        root: RootStartConfig,
        config: StarseadConfig,
    ): StarseadSnapshot {
        RootFailureWatcher.beginAttempt()
        val snapshot = status().boundSnapshot()
        if (snapshot != null && snapshot.owner != StarseadOwner.StarseaN) {
            throw RootRuntimeConflictException(snapshot)
        }
        return launch(
            root = root,
            config = config,
            restartExpectedOwner = snapshot?.owner,
            launchMode = RootPublicationLaunchMode.Service,
        )
    }

    suspend fun reconfigureServiceControl(
        root: RootStartConfig,
        config: StarseadConfig,
    ): Boolean {
        RootFailureWatcher.beginAttempt()
        val snapshot = status().boundSnapshot()
        if (snapshot != null && snapshot.owner != StarseadOwner.StarseaN) {
            throw RootRuntimeConflictException(snapshot)
        }
        val plan = try {
            serviceControlReconfigurePlan(snapshot?.phase, config.serviceControl.enabled)
        } catch (_: IllegalArgumentException) {
            throw RootRuntimeBusyException(requireNotNull(snapshot))
        }
        if (plan.shutdownRequired) shutdownOwn()
        when (plan.launchMode) {
            RootPublicationLaunchMode.Service -> launch(
                root,
                config,
                restartExpectedOwner = snapshot?.owner,
                launchMode = RootPublicationLaunchMode.Service,
            )
            RootPublicationLaunchMode.Monitor -> launch(
                root,
                config,
                restartExpectedOwner = snapshot?.owner,
                launchMode = RootPublicationLaunchMode.Monitor,
            )
            RootPublicationLaunchMode.None -> RootFailureWatcher.stop()
        }
        return plan.launchMode == RootPublicationLaunchMode.Service
    }

    suspend fun disableServiceControlWithoutConfig() {
        val snapshot = status().boundSnapshot() ?: run {
            RootFailureWatcher.stop()
            return
        }
        if (snapshot.owner != StarseadOwner.StarseaN) {
            throw RootRuntimeConflictException(snapshot)
        }
        if (snapshot.phase != StarseadPhase.Stopped) {
            throw RootRuntimeBusyException(snapshot)
        }
        shutdownOwn()
    }

    private suspend fun launch(
        root: RootStartConfig,
        config: StarseadConfig,
        restartExpectedOwner: StarseadOwner?,
        launchMode: RootPublicationLaunchMode,
    ): StarseadSnapshot {
        // Only an actual ROOT launch may arm diagnostics; constructing engines also happens in VPN.
        RootFailureWatcher.ensureStarted(appContext, shell, runtimeLayout, explicitRootAction = true, running = false)
        var stage = "prepare_directories"
        runCatching { AndroidAppLogger.info(LogTag, "root_start mode=${config.mode.wireValue} launch=$launchMode stage=$stage") }
        try {
            preparePublication()
            stage = "encode_config"
            val daemonConfigBytes = StarseadConfigEncoder.encode(config).toByteArray(Charsets.UTF_8)
            val publication = RootPublicationBundle(
                runtimeLayout = runtimeLayout,
                bootEnabled = root.enableBoot,
                launchMode = launchMode,
                restartExpectedOwner = restartExpectedOwner?.wireValue,
            )
            clearInMemoryServiceLogs()
            stage = "root_prepare"
            val preparationResult = shell.exec(
                RootPublicationCommand.buildPreparation(publication),
                ShellExecOptions(logFailure = false),
            )
            reportServiceLogCleanupFailures(preparationResult.stderr)
            if (preparationResult.errno != 0 || preparationResult.stdout.isNotBlank()) {
                throw launchFailure(preparationResult)
            }
            stage = "config_write"
            RootPublicationWriter.write(runtimeLayout, root.xrayConfigJson.toByteArray(Charsets.UTF_8), daemonConfigBytes)
            runCatching { AndroidAppLogger.info(LogTag, "root_start stage=config_write result=ok") }
            stage = "launch"
            val launchResult = shell.exec(
                RootPublicationCommand.buildLaunch(publication),
                ShellExecOptions(logFailure = false),
            )
            if (launchResult.errno != 0 || launchResult.stdout.isNotBlank()) {
                throw launchFailure(launchResult)
            }
            stage = "await_ready"
            runCatching { AndroidAppLogger.info(LogTag, "root_start stage=launch result=sent") }
            val acceptHeldStop = launchMode == RootPublicationLaunchMode.Service &&
                config.serviceControl.enabled
            val snapshot = withTimeoutOrNull(StartTimeoutMilliseconds.milliseconds) {
                when (launchMode) {
                    RootPublicationLaunchMode.Service -> if (acceptHeldStop) {
                        awaitRunningOrServiceControlHold(runtimeLayout.starseadPath)
                    } else {
                        client.awaitRunning(runtimeLayout.starseadPath)
                    }
                    RootPublicationLaunchMode.Monitor -> client.awaitStopped(runtimeLayout.starseadPath)
                    RootPublicationLaunchMode.None -> error("A non-launch publication has no runtime snapshot")
                }
            } ?: throw IllegalStateException("starsead did not reach the requested phase before timeout")
            if (snapshot.owner != StarseadOwner.StarseaN) throw RootRuntimeConflictException(snapshot)
            require(snapshot.mode == config.mode) { "Unexpected ROOT mode ${snapshot.mode.wireValue}" }
            if (snapshot.phase == StarseadPhase.Running) {
                observeRunningFailure(snapshot, explicitRootAction = true)
            } else {
                // A resident supervisor waiting for a trigger has no running core to monitor.
                RootFailureWatcher.stop()
            }
            runCatching { AndroidAppLogger.info(LogTag, "root_start stage=ready phase=${snapshot.phase}") }
            return snapshot
        } catch (error: Exception) {
            withContext(NonCancellable) { RootFailureWatcher.stop() }
            val outcome = if (error is kotlinx.coroutines.CancellationException) "cancelled" else "failed"
            runCatching { AndroidAppLogger.warn(LogTag, "root_start stage=$stage result=$outcome type=${error.javaClass.simpleName}") }
            throw error
        }
    }

    suspend fun stopOwn(): StarseadControlResponse {
        val initial = status()
        val initialSnapshot = initial.boundSnapshot() ?: run {
            RootFailureWatcher.stop()
            return initial
        }
        if (initialSnapshot.owner != StarseadOwner.StarseaN) {
            throw RootRuntimeConflictException(initialSnapshot)
        }
        val result = shell.exec(RootStopOwnCommand.build(runtimeLayout), ShellExecOptions(logFailure = false))
        val response = StarseadControlCodec.decodeShellResponse(result)
        when (response.requestId) {
            "status" -> response.boundSnapshot()?.let { snapshot ->
                if (snapshot.owner != StarseadOwner.StarseaN) throw RootRuntimeConflictException(snapshot)
            }
            "stop" -> Unit
            else -> error("Unexpected stop-own response id")
        }
        if (response.result.code == StarseadResultCode.Ok || response.result.code == StarseadResultCode.NotRunning) {
            RootFailureWatcher.stop()
            return response
        }
        error(response.result.message ?: "Failed to stop starsead")
    }

    suspend fun shutdownOwn(): StarseadControlResponse {
        val initial = status()
        val initialSnapshot = initial.boundSnapshot() ?: run {
            RootFailureWatcher.stop()
            return initial
        }
        if (initialSnapshot.owner != StarseadOwner.StarseaN) {
            throw RootRuntimeConflictException(initialSnapshot)
        }
        val result = shell.exec(
            RootShutdownOwnCommand.build(runtimeLayout),
            ShellExecOptions(logFailure = false),
        )
        val response = StarseadControlCodec.decodeShellResponse(result)
        when (response.requestId) {
            "status" -> response.boundSnapshot()?.let { snapshot ->
                if (snapshot.owner != StarseadOwner.StarseaN) {
                    throw RootRuntimeConflictException(snapshot)
                }
            }
            "shutdown", "stop" -> Unit
            else -> error("Unexpected shutdown-own response id")
        }
        if (response.result.code == StarseadResultCode.Ok ||
            response.result.code == StarseadResultCode.NotRunning
        ) {
            RootFailureWatcher.stop()
            return response
        }
        error(response.result.message ?: "Failed to shutdown starsead")
    }

    suspend fun publishBoot(
        root: RootStartConfig,
        config: StarseadConfig,
    ) {
        preparePublication()
        RootBootConfigWriter.write(
            layout = runtimeLayout,
            coreConfigBytes = root.xrayConfigJson.toByteArray(Charsets.UTF_8),
            encodedDaemonConfig = StarseadConfigEncoder.encode(config),
        )
        val result = shell.exec(
            RootBootPublicationCommand.buildInstallation(runtimeLayout),
            ShellExecOptions(logFailure = false),
        )
        requirePublicationSuccess(result)
    }

    private fun requirePublicationSuccess(result: ShellExecResult) {
        if (result.errno != 0 || result.stdout.isNotBlank()) throw launchFailure(result)
    }

    suspend fun removeBoot() {
        val result = shell.exec(
            RootBootPublicationCommand.buildRemoval(runtimeLayout),
            ShellExecOptions(logFailure = false),
        )
        requirePublicationSuccess(result)
    }

    private suspend fun awaitRunningOrServiceControlHold(executablePath: String): StarseadSnapshot {
        var delayMilliseconds = 50L
        var stoppedSinceNanoseconds: Long? = null
        while (true) {
            val snapshot = client.status(executablePath).boundSnapshot()
            if (snapshot != null && snapshot.owner == StarseadOwner.StarseaN) {
                when (snapshot.phase) {
                    StarseadPhase.Running -> return snapshot
                    StarseadPhase.Failed -> error(snapshot.error?.message ?: "starsead entered failed phase")
                    StarseadPhase.Stopped -> {
                        val since = stoppedSinceNanoseconds ?: System.nanoTime().also {
                            stoppedSinceNanoseconds = it
                        }
                        if (System.nanoTime() - since >= ServiceControlHoldSettleNanoseconds) {
                            return snapshot
                        }
                    }
                    else -> stoppedSinceNanoseconds = null
                }
            }
            delay(delayMilliseconds.milliseconds)
            delayMilliseconds = (delayMilliseconds * 2L).coerceAtMost(250L)
        }
    }

    private fun launchFailure(result: ShellExecResult): IllegalStateException {
        runCatching { AndroidAppLogger.warn(LogTag, "root_launcher exit=${result.errno} stderr=${sanitizeLauncherStderr(result.stderr).take(512)}") }
        val controlResponse = result.controlResponseOrNull()
        controlResponse?.result?.snapshot?.rejectBound(StarseadOwner.StarseaN)
        val message = controlResponse?.result?.message ?: sanitizeLauncherStderr(result.stderr)
            .ifBlank { "starsead launcher exited with ${result.errno}" }
        return IllegalStateException(message)
    }

    private fun preparePublication() {
        appContext.prepareRootPublicationDirectories()
    }

    private fun clearInMemoryServiceLogs() {
        runCatching { clearServiceLogRepositories() }.onFailure { error ->
            runCatching { AndroidAppLogger.warn(LogTag, "Failed to clear in-memory service logs", error) }
        }
    }

    private fun reportServiceLogCleanupFailures(stderr: String) {
        stderr.lineSequence()
            .filter { line -> line.startsWith(RootServiceLogCleanupWarningPrefix) }
            .forEach { warning -> runCatching { AndroidAppLogger.warn(LogTag, warning) } }
    }

}

private const val LogTag = "RootSupervisorController"
private const val StartTimeoutMilliseconds = 15_000L
private const val ServiceControlHoldSettleNanoseconds = 750_000_000L

internal fun sanitizeLauncherStderr(stderr: String): String {
    val retained = mutableListOf<String>()
    var readingFileContexts = false
    stderr.lineSequence().forEach { line ->
        if (line.startsWith(RootServiceLogCleanupWarningPrefix)) return@forEach
        if (line.trim() == "SELinux: Loaded file context from:") {
            readingFileContexts = true
            return@forEach
        }
        val trimmed = line.trim()
        if (
            readingFileContexts &&
            trimmed.startsWith('/') &&
            "/selinux/" in trimmed &&
            trimmed.endsWith("_file_contexts")
        ) {
            return@forEach
        }
        readingFileContexts = false
        retained += line
    }
    val stderrWithoutCleanupWarnings = stderr.lineSequence()
        .filterNot { line -> line.startsWith(RootServiceLogCleanupWarningPrefix) }
        .joinToString("\n")
        .trim()
    return retained.joinToString("\n").trim().ifBlank { stderrWithoutCleanupWarnings }
}
