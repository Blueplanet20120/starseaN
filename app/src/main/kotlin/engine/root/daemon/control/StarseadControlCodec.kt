// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package engine.root.daemon.control

import engine.root.daemon.config.StarseadCoreType
import engine.root.daemon.config.StarseadMode
import engine.root.daemon.config.StarseadOwner
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import system.ShellExecResult

internal object StarseadControlCodec {
    fun decodeResponse(payload: String): StarseadControlResponse {
        val root = parseClosedPayload(payload)
        root.requireExactKeys("protocolVersion", "requestId", "result")
        require(root.requiredInt("protocolVersion") == ProtocolVersion)
        val requestId = root.requiredString("requestId")
        require(RequestIdRegex.matches(requestId))
        val resultObject = root.requiredObject("result")
        resultObject.requireExactKeys("code", "snapshot", "message")
        val code = enumWire<StarseadResultCode>(resultObject.requiredString("code"))
        val snapshot = resultObject["snapshot"].toNullableObject()?.toSnapshot()
        val message = resultObject["message"].toNullableString()
        validateResultNullability(code, snapshot, message)
        return StarseadControlResponse(requestId, StarseadControlResult(code, snapshot, message))
    }

    fun decodeShellResponse(
        expectedRequestId: String,
        result: ShellExecResult,
    ): StarseadControlResponse {
        val response = decodeShellResponse(result)
        require(response.requestId == expectedRequestId)
        return response
    }

    fun decodeShellResponse(result: ShellExecResult): StarseadControlResponse {
        require('\n' !in result.stdout && '\r' !in result.stdout)
        val response = decodeResponse(result.stdout)
        val expectedExitCode = response.result.code.exitCode
        require(
            result.errno == expectedExitCode ||
                (result.errno == NormalizedNonZeroExitCode && expectedExitCode != 0),
        ) { "Shell exit ${result.errno} does not match protocol exit $expectedExitCode" }
        return response
    }

    fun decodeEvent(payload: String): StarseadControlEvent {
        val root = parseClosedPayload(payload)
        root.requireExactKeys("protocolVersion", "event")
        require(root.requiredInt("protocolVersion") == ProtocolVersion)
        val value = root.requiredObject("event")
        value.requireExactKeys("sequence", "type", "snapshot", "details")
        val event = StarseadControlEvent(
            sequence = value.requiredLong("sequence"),
            type = enumWire(value.requiredString("type")),
            snapshot = value.requiredObject("snapshot").toSnapshot(),
            details = value.getValue("details").toNullableObject()?.toControlError(),
        )
        require(event.sequence > 0)
        return event
    }

    private fun JsonObject.toSnapshot(): StarseadSnapshot {
        requireExactKeys(
            "phase", "owner", "coreType", "mode", "supervisorPid", "corePid",
            "helperType", "helperPid", "matcherConfigured", "matcherActive", "rules",
            "network", "error",
        )
        val rulesObject = requiredObject("rules")
        rulesObject.requireExactKeys("active", "generation", "categories")
        val categories = rulesObject.requiredArray("categories").map { element ->
            enumWire<StarseadRuleCategory>(element.requiredStringValue())
        }
        require(categories.distinct() == categories && categories == categories.sortedBy { it.ordinal })
        val rules = StarseadRulesSnapshot(
            active = rulesObject.requiredBoolean("active"),
            generation = rulesObject.requiredLong("generation"),
            categories = categories,
        )
        require(if (rules.active) rules.generation > 0 && rules.categories.isNotEmpty() else rules.generation == 0L && rules.categories.isEmpty())

        val networkObject = requiredObject("network")
        networkObject.requireExactKeys("ipv4Ready", "ipv6Enabled", "ipv6Ready")
        val network = StarseadNetworkSnapshot(
            ipv4Ready = networkObject.requiredBoolean("ipv4Ready"),
            ipv6Enabled = networkObject.requiredBoolean("ipv6Enabled"),
            ipv6Ready = networkObject.requiredBoolean("ipv6Ready"),
        )
        val snapshot = StarseadSnapshot(
            phase = enumWire(requiredString("phase")),
            owner = enumWire(requiredString("owner")),
            coreType = enumWire(requiredString("coreType")),
            mode = StarseadMode.fromWire(requiredString("mode")),
            supervisorPid = requiredInt("supervisorPid"),
            corePid = getValue("corePid").toNullableInt(),
            helperType = getValue("helperType").toNullableString()?.let(::enumWire),
            helperPid = getValue("helperPid").toNullableInt(),
            matcherConfigured = requiredBoolean("matcherConfigured"),
            matcherActive = requiredBoolean("matcherActive"),
            rules = rules,
            network = network,
            error = getValue("error").toNullableObject()?.toControlError(),
        )
        snapshot.validate()
        return snapshot
    }
}

internal val StarseadResultCode.exitCode: Int
    get() = when (this) {
        StarseadResultCode.Ok -> 0
        StarseadResultCode.AlreadyRunning -> 4
        StarseadResultCode.NotRunning -> 3
        StarseadResultCode.PermissionDenied -> 77
        StarseadResultCode.InvalidRequest,
        StarseadResultCode.ConfigInvalid,
        StarseadResultCode.UnsupportedCombination,
        -> 64
        StarseadResultCode.StartFailed,
        StarseadResultCode.StopFailed,
        StarseadResultCode.InternalError,
        -> 1
    }

private fun StarseadSnapshot.validate() {
    require(supervisorPid > 0 && (corePid == null || corePid > 0) && (helperPid == null || helperPid > 0))
    requireOwnerCore(owner, coreType)
    val expectedHelper = when (mode) {
        StarseadMode.Tun2Socks -> StarseadHelperType.HevSocks5Tunnel
        StarseadMode.Bpf2Socks -> StarseadHelperType.Bpf2Socks
        StarseadMode.Tproxy, StarseadMode.Tun, StarseadMode.Ebpf -> null
    }
    require(helperType == expectedHelper)
    require(helperType != null || helperPid == null)
    require(!matcherActive || matcherConfigured)
    if (mode == StarseadMode.Tun || mode == StarseadMode.Ebpf) {
        require(!matcherConfigured && !matcherActive)
        require(!rules.active && rules.generation == 0L && rules.categories.isEmpty())
    }
    if (phase == StarseadPhase.Running) {
        require(network.ipv4Ready && network.ipv6Ready && corePid != null && error == null)
        require(helperType == null || helperPid != null)
        require(!matcherConfigured || matcherActive)
        require(mode == StarseadMode.Tun || mode == StarseadMode.Ebpf || rules.active)
    } else {
        require(!network.ipv4Ready && !network.ipv6Ready)
    }
    require(phase != StarseadPhase.Failed || error != null)
}

private fun requireOwnerCore(
    owner: StarseadOwner,
    coreType: StarseadCoreType,
) {
    require(
        (owner == StarseadOwner.StarseaN && coreType == StarseadCoreType.Xray) ||
            (owner == StarseadOwner.AsteriskBox && coreType == StarseadCoreType.SingBox) ||
            (owner == StarseadOwner.AsteriskMeta && coreType == StarseadCoreType.Mihomo),
    )
}

private fun JsonObject.toControlError(): StarseadControlError {
    requireExactKeys("code", "component", "message", "exitCode", "signal")
    val error = StarseadControlError(
        code = enumWire(requiredString("code")),
        component = enumWire(requiredString("component")),
        message = requiredString("message"),
        exitCode = getValue("exitCode").toNullableInt(),
        signal = getValue("signal").toNullableInt(),
    )
    require(error.message.isNotEmpty())
    if (error.code == StarseadFailureCode.ChildExited) {
        require((error.exitCode != null) xor (error.signal != null))
    } else {
        require(error.exitCode == null && error.signal == null)
    }
    return error
}

private fun validateResultNullability(code: StarseadResultCode, snapshot: StarseadSnapshot?, message: String?) {
    when (code) {
        StarseadResultCode.Ok -> require(snapshot != null && message == null)
        StarseadResultCode.AlreadyRunning -> require(snapshot != null && !message.isNullOrEmpty())
        StarseadResultCode.StopFailed -> require(snapshot?.phase == StarseadPhase.Failed && !message.isNullOrEmpty())
        StarseadResultCode.InternalError -> require(!message.isNullOrEmpty())
        StarseadResultCode.NotRunning,
        StarseadResultCode.PermissionDenied,
        StarseadResultCode.InvalidRequest,
        StarseadResultCode.ConfigInvalid,
        StarseadResultCode.UnsupportedCombination,
        StarseadResultCode.StartFailed,
        -> require(snapshot == null && !message.isNullOrEmpty())
    }
}

private fun JsonObject.requireExactKeys(vararg keys: String) {
    require(this.keys == keys.toSet())
}

private fun JsonObject.requiredString(key: String): String = getValue(key).requiredStringValue()

private fun JsonElement.requiredStringValue(): String {
    require(this is JsonPrimitive && isString)
    return content
}

private fun JsonObject.requiredInt(key: String): Int = getValue(key).jsonPrimitive.content.toInt()
private fun JsonObject.requiredLong(key: String): Long = getValue(key).jsonPrimitive.content.toLong()
private fun JsonObject.requiredBoolean(key: String): Boolean =
    requireNotNull(getValue(key).jsonPrimitive.booleanOrNull)
private fun JsonObject.requiredObject(key: String): JsonObject = getValue(key).jsonObject
private fun JsonObject.requiredArray(key: String): JsonArray = getValue(key).jsonArray

private fun JsonElement?.toNullableObject(): JsonObject? = when (this) {
    null, JsonNull -> null
    else -> jsonObject
}

private fun JsonElement?.toNullableString(): String? = when (this) {
    null, JsonNull -> null
    else -> jsonPrimitive.contentOrNull.also { require(jsonPrimitive.isString) }
}

private fun JsonElement.toNullableInt(): Int? = if (this === JsonNull) null else jsonPrimitive.content.toInt()
private inline fun <reified T : Enum<T>> enumWire(value: String): T = enumValues<T>().firstOrNull { entry ->
    val wire = when (entry) {
        is StarseadOwner -> entry.wireValue
        is StarseadCoreType -> entry.wireValue
        is StarseadPhase -> entry.wireValue
        is StarseadResultCode -> entry.wireValue
        is StarseadEventType -> entry.wireValue
        is StarseadHelperType -> entry.wireValue
        is StarseadRuleCategory -> entry.wireValue
        is StarseadFailureCode -> entry.wireValue
        is StarseadComponent -> entry.wireValue
        else -> error("Unsupported wire enum")
    }
    wire == value
} ?: throw IllegalArgumentException("Unknown protocol token")

private val RequestIdRegex = Regex("[A-Za-z0-9._-]{1,64}")
private const val ProtocolVersion = 1
private const val NormalizedNonZeroExitCode = 1
