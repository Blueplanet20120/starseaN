// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package engine.root.daemon.control

import engine.root.daemon.config.StarseadCoreType
import engine.root.daemon.config.StarseadMode
import engine.root.daemon.config.StarseadOwner

internal enum class StarseadPhase(val wireValue: String) {
    Validating("validating"), Acquiring("acquiring"),
    Starting("starting"), ApplyingRules("applying-rules"), Running("running"),
    Stopping("stopping"), Stopped("stopped"), Failed("failed"), Paused("paused"),
}

internal enum class StarseadResultCode(val wireValue: String) {
    Ok("ok"), AlreadyRunning("already_running"), NotRunning("not_running"),
    PermissionDenied("permission_denied"), InvalidRequest("invalid_request"),
    ConfigInvalid("config_invalid"), UnsupportedCombination("unsupported_combination"),
    StartFailed("start_failed"), StopFailed("stop_failed"), InternalError("internal_error"),
}

internal enum class StarseadHelperType(val wireValue: String) {
    HevSocks5Tunnel("hev-socks5-tunnel"), Bpf2Socks("bpf2socks"),
}

internal enum class StarseadEventType(val wireValue: String) {
    Starting("starting"), Running("running"), RulesChanged("rules-changed"),
    Stopping("stopping"), Stopped("stopped"), CoreExited("core-exited"),
    HelperFailed("helper-failed"), Failed("failed"), Paused("paused"),
}

internal enum class StarseadRuleCategory(val wireValue: String) {
    Tproxy("tproxy"), Routing("routing"), Dns("dns"), FakeDns("fake-dns"),
    LocalBypass("local-bypass"), Hotspot("hotspot"), Tc("tc"), Bpf("bpf"), Ipv6Guard("ipv6-guard"),
}

internal enum class StarseadFailureCode(val wireValue: String) {
    StartFailed("start_failed"), ReadinessTimeout("readiness_timeout"), ChildExited("child_exited"),
    StateInvalid("state_invalid"), StateIncompatible("state_incompatible"),
    ResourceCollision("resource_collision"), IoError("io_error"),
    StopFailed("stop_failed"), InternalError("internal_error"),
}

internal enum class StarseadComponent(val wireValue: String) {
    Runtime("runtime"), Core("core"), Helper("helper"), Matcher("matcher"),
    Rules("rules"), Network("network"), State("state"), Log("log"), Control("control"),
}

internal data class StarseadControlError(
    val code: StarseadFailureCode,
    val component: StarseadComponent,
    val message: String,
    val exitCode: Int?,
    val signal: Int?,
)

internal data class StarseadRulesSnapshot(
    val active: Boolean,
    val generation: Long,
    val categories: List<StarseadRuleCategory>,
)

internal data class StarseadNetworkSnapshot(
    val ipv4Ready: Boolean,
    val ipv6Enabled: Boolean,
    val ipv6Ready: Boolean,
)

internal data class StarseadSnapshot(
    val phase: StarseadPhase,
    val owner: StarseadOwner,
    val coreType: StarseadCoreType,
    val mode: StarseadMode,
    val supervisorPid: Int,
    val corePid: Int?,
    val helperType: StarseadHelperType?,
    val helperPid: Int?,
    val matcherConfigured: Boolean,
    val matcherActive: Boolean,
    val rules: StarseadRulesSnapshot,
    val network: StarseadNetworkSnapshot,
    val error: StarseadControlError?,
)

internal data class StarseadControlResult(
    val code: StarseadResultCode,
    val snapshot: StarseadSnapshot?,
    val message: String?,
)

internal data class StarseadControlResponse(
    val requestId: String,
    val result: StarseadControlResult,
)

internal data class StarseadControlEvent(
    val sequence: Long,
    val type: StarseadEventType,
    val snapshot: StarseadSnapshot,
    val details: StarseadControlError?,
)
