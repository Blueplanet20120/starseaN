// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package engine.root.daemon

import engine.root.daemon.control.StarseadControlCodec
import engine.root.daemon.control.StarseadControlResponse
import engine.root.daemon.control.StarseadEventType
import engine.root.daemon.control.StarseadPhase
import engine.root.daemon.control.StarseadResultCode
import engine.root.daemon.control.StarseadSnapshot
import features.logs.AndroidAppLogger
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.channelFlow
import kotlinx.coroutines.isActive
import system.RootShellGateway
import system.ShellExecOptions
import utils.shellQuote
import kotlin.time.Duration.Companion.milliseconds

internal class StarseadClient(
    private val shell: RootShellGateway,
    private val watchRetryDelaysMilliseconds: List<Long> = DefaultWatchRetryDelaysMilliseconds,
) {
    suspend fun status(executablePath: String): StarseadControlResponse = runControl(executablePath, "status")

    suspend fun stop(executablePath: String): StarseadControlResponse = runControl(executablePath, "stop")

    suspend fun shutdown(executablePath: String): StarseadControlResponse =
        runControl(executablePath, "shutdown")

    fun observeStatus(executablePath: String): Flow<StarseadSnapshot> = channelFlow {
        var retryIndex = 0
        while (isActive) {
            val stream = StatusWatchStream { snapshot -> trySend(snapshot) }
            val result = shell.execStreaming(
                "${executablePath.shellQuote()} watch",
                ShellExecOptions(logFailure = false),
                stream::accept,
            )
            when (stream.termination(result)) {
                WatchTermination.FinalEvent -> retryIndex = 0
                WatchTermination.NotRunning,
                WatchTermination.Disconnected,
                -> {
                    val delayMilliseconds = watchRetryDelaysMilliseconds.getOrNull(retryIndex++) ?: break
                    delay(delayMilliseconds.milliseconds)
                }
            }
        }
    }

    suspend fun awaitStopped(executablePath: String): StarseadSnapshot {
        var delayMilliseconds = InitialWatchRetryDelayMilliseconds
        while (true) {
            val response = status(executablePath)
            response.result.snapshot?.takeIf { it.phase == StarseadPhase.Stopped }?.let { return it }
            check(response.result.code == StarseadResultCode.NotRunning ||
                response.result.code == StarseadResultCode.Ok
            ) { response.result.message ?: "starsead monitor failed" }
            delay(delayMilliseconds.milliseconds)
            delayMilliseconds = (delayMilliseconds * 2L).coerceAtMost(MaxWatchRetryDelayMilliseconds)
        }
    }

    suspend fun awaitRunning(executablePath: String): StarseadSnapshot {
        val command =
            "timeout -k 1s ${WatchProcessTimeoutSeconds}s " +
                "${executablePath.shellQuote()} watch --until-running"
        var retryDelayMilliseconds = InitialWatchRetryDelayMilliseconds
        while (true) {
            val stream = RunningWatchStream()
            val result = shell.execStreaming(
                command,
                ShellExecOptions(logFailure = false),
                stream::accept,
            )
            if (result.stderr.isNotBlank()) {
                runCatching { AndroidAppLogger.warn(LogTag, "root_watch exit=${result.errno} stderr=${result.stderr.take(512)}") }
            }
            stream.runningSnapshot?.let { return it }
            if (stream.retryWhenUnbound) {
                delay(retryDelayMilliseconds.milliseconds)
                retryDelayMilliseconds = (retryDelayMilliseconds * 2L)
                    .coerceAtMost(MaxWatchRetryDelayMilliseconds)
                continue
            }
            error(
                result.stderr.ifBlank {
                    "starsead watch ended before a running or failed event"
                },
            )
        }
    }

    private suspend fun runControl(
        executablePath: String,
        requestId: String,
    ): StarseadControlResponse {
        val command = "${executablePath.shellQuote()} $requestId"
        val result = shell.exec(command, ShellExecOptions(logFailure = false))
        return runCatching {
            StarseadControlCodec.decodeShellResponse(requestId, result)
        }.getOrElse { error ->
            AndroidAppLogger.error(
                LogTag,
                "invalid_control_response request=$requestId errno=${result.errno} " +
                    "stdout=${result.stdout} stderr=${result.stderr}",
                error,
            )
            throw error
        }
    }

    private companion object {
        const val LogTag = "StarseadClient"
        const val InitialWatchRetryDelayMilliseconds = 10L
        const val MaxWatchRetryDelayMilliseconds = 250L
        const val WatchProcessTimeoutSeconds = 16L
        val DefaultWatchRetryDelaysMilliseconds = listOf(25L, 50L, 100L, 200L, 400L, 800L)
    }

    private enum class WatchTermination {
        FinalEvent,
        NotRunning,
        Disconnected,
    }

    private class StatusWatchStream(
        private val onSnapshot: (StarseadSnapshot) -> Unit,
    ) {
        private var initialReceived = false
        private var lastSequence = 0L
        private var terminalEventReceived = false
        private var notRunningReceived = false

        fun accept(line: String) {
            if (!initialReceived) {
                val response = StarseadControlCodec.decodeResponse(line)
                require(response.requestId == "watch")
                initialReceived = true
                if (response.result.code == StarseadResultCode.NotRunning) {
                    notRunningReceived = true
                    return
                }
                check(response.result.code == StarseadResultCode.Ok) {
                    response.result.message ?: "starsead watch request failed"
                }
                onSnapshot(requireNotNull(response.result.snapshot))
                return
            }
            check(!notRunningReceived && !terminalEventReceived) {
                "starsead watch emitted data after completion"
            }
            val event = StarseadControlCodec.decodeEvent(line)
            require(event.sequence > lastSequence) { "starsead watch event sequence regressed" }
            lastSequence = event.sequence
            onSnapshot(event.snapshot)
            terminalEventReceived = event.type == StarseadEventType.Stopped ||
                event.type == StarseadEventType.Failed
        }

        fun termination(result: system.ShellExecResult): WatchTermination {
            if (terminalEventReceived) return WatchTermination.FinalEvent
            if (notRunningReceived) return WatchTermination.NotRunning
            if (!initialReceived && result.stderr.isNotBlank()) {
                AndroidAppLogger.warn(LogTag, "starsead watch disconnected: ${result.stderr}")
            }
            return WatchTermination.Disconnected
        }
    }

    private class RunningWatchStream {
        @Volatile
        var runningSnapshot: StarseadSnapshot? = null
            private set

        @Volatile
        var retryWhenUnbound: Boolean = false
            private set

        private var initialReceived = false
        private var lastSequence = 0L

        fun accept(line: String) {
            if (!initialReceived) {
                val response = StarseadControlCodec.decodeResponse(line)
                require(response.requestId == "watch")
                initialReceived = true
                if (response.result.code == StarseadResultCode.NotRunning) {
                    retryWhenUnbound = true
                    return
                }
                check(response.result.code == StarseadResultCode.Ok) {
                    response.result.message ?: "starsead watch request failed"
                }
                val snapshot = requireNotNull(response.result.snapshot)
                if (snapshot.phase == StarseadPhase.Stopped) {
                    retryWhenUnbound = true
                    return
                }
                inspect(snapshot)
                return
            }
            val event = StarseadControlCodec.decodeEvent(line)
            require(event.sequence > lastSequence) { "starsead watch event sequence regressed" }
            lastSequence = event.sequence
            if (event.type == StarseadEventType.Failed ||
                event.snapshot.phase == StarseadPhase.Failed
            ) {
                error(
                    event.details?.message
                        ?: event.snapshot.error?.message
                        ?: "starsead entered failed phase",
                )
            }
            inspect(event.snapshot)
        }

        private fun inspect(snapshot: StarseadSnapshot) {
            if (snapshot.phase == StarseadPhase.Failed) {
                error(snapshot.error?.message ?: "starsead entered failed phase")
            }
            if (snapshot.phase == StarseadPhase.Running) {
                runningSnapshot = snapshot
                return
            }
            check(snapshot.phase != StarseadPhase.Stopped) {
                "starsead stopped before reaching running phase"
            }
        }
    }
}
