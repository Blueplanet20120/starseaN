// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package engine.root.publication

import utils.shellQuote

internal object RootPublicationCommand {
    fun buildPreparation(bundle: RootPublicationBundle): String {
        val layout = bundle.runtimeLayout
        return buildString {
            appendLine("set -eu")
            RootPublicationRequiredTools.forEach { tool ->
                appendLine("command -v $tool >/dev/null 2>&1 || { printf '%s\\n' 'root_start missing_tool=$tool' >&2; exit 70; }")
            }
            appendLine("[ -x ${layout.starseadPath.shellQuote()} ] || exit 70")
            appendLine("[ -d ${layout.dataDir.shellQuote()} ] || exit 70")
            bundle.restartExpectedOwner?.let { owner ->
                appendConditionalStop(layout, owner)
            }
            appendStatusMustBePublishable(layout)
            appendServiceLogCleanup(layout)
        }.trimEnd()
    }

    fun buildLaunch(bundle: RootPublicationBundle): String {
        val layout = bundle.runtimeLayout
        return buildString {
            appendLine("set -eu")
            appendStatusMustBePublishable(layout)
            listOf(layout.configPath, layout.starseadConfigPath).forEach { path ->
                appendLine("[ -f ${path.shellQuote()} ] && [ ! -L ${path.shellQuote()} ] || exit 70")
            }
            if (bundle.bootEnabled) {
                RootBootPublicationCommand.appendInstallBoot(this, layout)
            } else {
                RootBootPublicationCommand.appendRemoveBoot(this, layout)
            }
            val launchCommand = when (bundle.launchMode) {
                RootPublicationLaunchMode.None -> null
                RootPublicationLaunchMode.Service -> "start"
                RootPublicationLaunchMode.Monitor -> "monitor"
            }
            if (launchCommand != null) {
                appendLine(
                    "nohup ${layout.starseadPath.shellQuote()} $launchCommand " +
                        "--config ${layout.starseadConfigPath.shellQuote()} " +
                        "</dev/null >/dev/null 2>>${layout.starseadLogPath.shellQuote()} &",
                )
            }
        }.trimEnd()
    }

    private fun StringBuilder.appendStatusMustBePublishable(layout: RootRuntimeLayout) {
        appendLine("set +e")
        appendLine("starsead_status=\"$(${layout.starseadPath.shellQuote()} status)\"")
        appendLine("starsead_status_code=\"$?\"")
        appendLine("set -e")
        appendLine("if [ \"\$starsead_status_code\" -ne 3 ]; then")
        appendLine("  printf '%s\\n' \"\$starsead_status\"")
        appendLine("  exit \"\$starsead_status_code\"")
        appendLine("fi")
    }

    private fun StringBuilder.appendServiceLogCleanup(layout: RootRuntimeLayout) {
        appendLine(
            "for service_log in ${layout.logDirectoryPath.shellQuote()}/* " +
                "${layout.logDirectoryPath.shellQuote()}/.[!.]* ${layout.logDirectoryPath.shellQuote()}/..?*; do",
        )
        appendLine("  [ -e \"\$service_log\" ] || continue")
        appendLine("  [ \"\${service_log##*/}\" = 'logcat.log' ] && continue")
        appendLine("  [ -f \"\$service_log\" ] && [ ! -L \"\$service_log\" ] || continue")
        appendLine("  rm -f -- \"\$service_log\" >/dev/null 2>&1 || { printf '%s\\n' \"$RootServiceLogCleanupWarningPrefix\$service_log\" >&2 || :; }")
        appendLine("done")
    }

    private fun StringBuilder.appendConditionalStop(
        layout: RootRuntimeLayout,
        expectedOwner: String,
    ) {
        appendLine("set +e")
        appendLine("starsead_status=\"$(${layout.starseadPath.shellQuote()} status)\"")
        appendLine("starsead_status_code=\"\$?\"")
        appendLine("set -e")
        appendLine("if [ \"\$starsead_status_code\" -ne 3 ]; then")
        appendLine("  case \"\$starsead_status\" in")
        appendLine("    *'\"owner\":\"$expectedOwner\"'*) ;;")
        appendLine("    *) printf '%s\\n' \"\$starsead_status\"; exit \"\$starsead_status_code\" ;;")
        appendLine("  esac")
        appendLine("  set +e")
        appendLine("  starsead_shutdown=\"$(${layout.starseadPath.shellQuote()} shutdown)\"")
        appendLine("  starsead_shutdown_code=\"\$?\"")
        appendLine("  set -e")
        appendLine("  [ \"\$starsead_shutdown_code\" -eq 0 ] || [ \"\$starsead_shutdown_code\" -eq 3 ] || { printf '%s\\n' \"\$starsead_shutdown\"; exit \"\$starsead_shutdown_code\"; }")
        appendLine("  starsead_attempt=0")
        appendLine("  while [ \"\$starsead_attempt\" -lt $SocketReleasePollAttempts ]; do")
        appendLine("    set +e")
        appendLine("    ${layout.starseadPath.shellQuote()} status >/dev/null")
        appendLine("    starsead_status_code=\"\$?\"")
        appendLine("    set -e")
        appendLine("    [ \"\$starsead_status_code\" -eq 3 ] && break")
        appendLine("    starsead_attempt=\$((starsead_attempt + 1))")
        appendLine("    sleep 0.1")
        appendLine("  done")
        appendLine("  [ \"\$starsead_status_code\" -eq 3 ] || exit 75")
        appendLine("fi")
    }

}

private const val SocketReleasePollAttempts = 50
internal const val RootServiceLogCleanupWarningPrefix = "Failed to clear service log: "
internal val RootPublicationRequiredTools = listOf(
    "grep",
    "mkdir",
    "chmod",
    "rm",
    "sleep",
    "nohup",
)
