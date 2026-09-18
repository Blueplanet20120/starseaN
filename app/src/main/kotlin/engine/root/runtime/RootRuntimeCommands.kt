// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package engine.root.runtime

import engine.root.publication.RootRuntimeLayout
import utils.shellQuote

internal object RootStopOwnCommand {
    fun build(layout: RootRuntimeLayout): String = buildString {
        appendLine("(")
        appendLine("set -eu")
        appendLine("set +e")
        appendLine("starsead_status=\"$(${layout.starseadPath.shellQuote()} status)\"")
        appendLine("starsead_status_code=\"\$?\"")
        appendLine("set -e")
        appendLine("case \"\$starsead_status\" in")
        appendLine("  *'\"owner\":\"starsean\"'*) ;;")
        appendLine("  *) printf '%s\\n' \"\$starsead_status\"; exit \"\$starsead_status_code\" ;;")
        appendLine("esac")
        appendLine("set +e")
        appendLine("starsead_stop=\"$(${layout.starseadPath.shellQuote()} stop)\"")
        appendLine("starsead_stop_code=\"\$?\"")
        appendLine("set -e")
        appendLine("[ \"\$starsead_stop_code\" -eq 0 ] || { printf '%s\\n' \"\$starsead_stop\"; exit \"\$starsead_stop_code\"; }")
        appendLine("printf '%s\\n' \"\$starsead_stop\"")
        appendLine(")")
    }.trimEnd()
}

internal object RootShutdownOwnCommand {
    fun build(layout: RootRuntimeLayout): String {
        val base = RootStopOwnCommand.build(layout)
            .replace("starsead_stop", "starsead_shutdown")
            .replace(" stop)", " shutdown)")
        val failureCheck =
            "[ \"\$starsead_shutdown_code\" -eq 0 ] || { printf '%s\\n' \"\$starsead_shutdown\"; " +
                "exit \"\$starsead_shutdown_code\"; }"
        val shutdown = base.replace(
            failureCheck,
            buildString {
                appendLine("case \"\$starsead_shutdown\" in")
                appendLine("  *'\"code\":\"invalid_request\"'*)")
                appendLine("    set +e")
                appendLine("    starsead_shutdown=\"\$(${layout.starseadPath.shellQuote()} stop)\"")
                appendLine("    starsead_shutdown_code=\"\$?\"")
                appendLine("    set -e")
                appendLine("    ;;")
                appendLine("esac")
                append(failureCheck)
            },
        )
        check(shutdown != base) { "Shutdown command failure check was not found" }
        val responsePrint = "printf '%s\\n' \"\$starsead_shutdown\""
        val shutdownWithoutEagerResponse = shutdown.substringBeforeLast(responsePrint)
        check(shutdownWithoutEagerResponse != shutdown) { "Shutdown response print was not found" }
        return shutdownWithoutEagerResponse + buildString {
            appendLine("starsead_attempt=0")
            appendLine("while [ \"\$starsead_attempt\" -lt $SocketReleasePollAttempts ]; do")
            appendLine("  set +e")
            appendLine("  grep -q '[[:space:]]@starsead[.]control\$' /proc/net/unix")
            appendLine("  starsead_socket_probe_code=\"\$?\"")
            appendLine("  set -e")
            appendLine("  case \"\$starsead_socket_probe_code\" in")
            appendLine("    0) ;;")
            appendLine("    1) printf '%s\\n' \"\$starsead_shutdown\"; exit 0 ;;")
            appendLine("    *) printf '%s\\n' \"\$starsead_shutdown\"; exit \"\$starsead_socket_probe_code\" ;;")
            appendLine("  esac")
            appendLine("  starsead_attempt=\$((starsead_attempt + 1))")
            appendLine("  sleep 0.1")
            appendLine("done")
            appendLine("printf '%s\\n' \"\$starsead_shutdown\"")
            appendLine("exit 1")
            append(")")
        }
    }
}

private const val SocketReleasePollAttempts = 50
