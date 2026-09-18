// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package features.resources.runtime

import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.concurrent.TimeUnit
import utils.shellQuote

data class XrayCoreRelease(
    val tag: String,
    val title: String,
    val body: String,
    val htmlUrl: String,
    val assetName: String,
    val downloadUrl: String,
)

data class XrayCoreReleaseCheck(
    val installedVersion: String,
    val latest: XrayCoreRelease,
    val isNewer: Boolean,
)

internal class AndroidXrayCoreReleaseClient(
    private val downloader: AndroidResourceFileDownloader,
) {
    fun fetchLatest(proxy: AndroidResourceFileDownloadProxy?): XrayCoreRelease {
        val abi = currentRuntimeAbi()
        val assetName = xrayAndroidAssetName(abi)
        return fetchLatestFromApi(proxy, abi, assetName)
    }

    private fun fetchLatestFromApi(
        proxy: AndroidResourceFileDownloadProxy?,
        abi: String,
        assetName: String,
    ): XrayCoreRelease {
        val payload = downloader.fetchText(
            url = ReleasesApiUrl,
            proxy = proxy,
            extraHeaders = mapOf(
                "Accept" to "application/vnd.github+json",
                "X-GitHub-Api-Version" to "2022-11-28",
            ),
        )
        val newest = pickNewestAndroidRelease(payload, abi)
        AndroidResourceFileLogger.info("Xray-core latest api tag=${newest.tag} asset=$assetName")
        return newest
    }
}

internal fun pickNewestAndroidRelease(payload: String, abi: String): XrayCoreRelease {
    val trimmed = payload.trim()
    require(trimmed.startsWith("[")) { "Unexpected Xray-core releases response" }
    val releases = JSONArray(trimmed)
    var newest: XrayCoreRelease? = null
    for (index in 0 until releases.length()) {
        val json = releases.optJSONObject(index) ?: continue
        if (json.optBoolean("draft")) continue
        val release = parseReleaseIfHasAndroidAsset(json, abi) ?: continue
        if (newest == null || compareXrayCoreVersion(release.tag, newest.tag) > 0) {
            newest = release
        }
    }
    return newest ?: error("No ${xrayAndroidAssetName(abi)} in recent Xray-core releases")
}

internal fun parseReleaseIfHasAndroidAsset(json: JSONObject, abi: String): XrayCoreRelease? {
    val tag = json.optString("tag_name").trim()
    if (tag.isEmpty()) return null
    val assets = json.optJSONArray("assets") ?: return null
    val assetName = xrayAndroidAssetName(abi)
    var downloadUrl = ""
    for (index in 0 until assets.length()) {
        val asset = assets.optJSONObject(index) ?: continue
        if (asset.optString("name").equals(assetName, ignoreCase = true)) {
            downloadUrl = asset.optString("browser_download_url").trim()
            break
        }
    }
    if (downloadUrl.isEmpty()) return null
    val title = json.optString("name").trim().ifEmpty { "Xray-core $tag" }
    return XrayCoreRelease(
        tag = tag,
        title = title,
        body = json.optString("body").trim(),
        htmlUrl = json.optString("html_url").trim(),
        assetName = assetName,
        downloadUrl = downloadUrl,
    )
}

internal fun probeXrayCoreVersion(binary: File): String? {
    if (!binary.isFile || binary.length() <= 0L) return null
    val scanned = scanXrayCoreVersion(binary)
    if (scanned != null) {
        AndroidResourceFileLogger.info("Xray-core scanned ${binary.name}=$scanned size=${binary.length()}")
        return scanned
    }
    val executed = executeXrayCoreVersion(binary)
    if (executed != null) {
        AndroidResourceFileLogger.info("Xray-core executed ${binary.name}=$executed")
        return executed
    }
    return null
}

internal suspend fun probeXrayCoreVersionWithShell(
    binary: File,
    exec: suspend (String) -> String,
): String? {
    if (!binary.isFile || binary.length() <= 0L) return null
    val quoted = binary.absolutePath.shellQuote()
    val output = exec("chmod 755 $quoted >/dev/null 2>&1; $quoted version")
    val version = parseXrayVersionOutput(output)
    if (version != null) {
        AndroidResourceFileLogger.info("Xray-core shell ${binary.name}=$version size=${binary.length()}")
        return version
    }
    AndroidResourceFileLogger.warn(
        "Xray-core version probe failed for ${binary.absolutePath} size=${binary.length()}",
    )
    return null
}

internal fun scanXrayCoreVersion(binary: File): String? {
    val overlap = 96
    val buffer = ByteArray(128 * 1024)
    var prefix = ByteArray(0)
    binary.inputStream().use { input ->
        while (true) {
            val read = input.read(buffer)
            if (read <= 0) break
            val chunk = if (prefix.isEmpty()) {
                buffer.copyOf(read)
            } else {
                prefix + buffer.copyOf(read)
            }
            val text = String(chunk, Charsets.ISO_8859_1)
            val scanned = xrayCoreVersionFromBinaryText(text)
            if (scanned != null) return scanned
            prefix = chunk.copyOfRange((chunk.size - overlap).coerceAtLeast(0), chunk.size)
        }
    }
    return null
}

private fun executeXrayCoreVersion(binary: File): String? {
    return runCatching {
        binary.setExecutable(true, false)
        val process = ProcessBuilder(binary.absolutePath, "version")
            .redirectErrorStream(true)
            .start()
        val finished = process.waitFor(5, TimeUnit.SECONDS)
        if (!finished) {
            process.destroyForcibly()
            return@runCatching null
        }
        parseXrayVersionOutput(process.inputStream.bufferedReader().readText())
    }.getOrNull()
}

internal fun parseXrayVersionOutput(output: String): String? {
    val match = XrayVersionPattern.find(output)
        ?: EmbeddedXrayVersionPattern.find(output)
        ?: return null
    return normalizeXrayCoreVersion(match.groupValues[1])
}

internal fun xrayCoreTagFromUrl(url: String): String? {
    val match = DownloadTagPattern.find(url) ?: return null
    return normalizeXrayCoreVersion(match.groupValues[1])
}

internal fun isNewerXrayCoreVersion(latest: String, installed: String): Boolean {
    if (versionParts(installed).isEmpty()) return true
    return compareXrayCoreVersion(latest, installed) > 0
}

internal fun xrayCoreVersionFromName(name: String): String? {
    val match = FileNameVersionPattern.find(name) ?: return null
    return normalizeXrayCoreVersion(match.groupValues[1])
}

internal fun xrayCoreVersionFromBinaryText(text: String): String? {
    val patterns = listOf(
        EmbeddedXrayVersionPattern,
        GoModuleVersionPattern,
    )
    for (pattern in patterns) {
        val match = pattern.find(text) ?: continue
        return normalizeXrayCoreVersion(match.groupValues[1])
    }
    return null
}

internal fun compareXrayCoreVersion(left: String, right: String): Int {
    val leftParts = versionParts(left)
    val rightParts = versionParts(right)
    val size = maxOf(leftParts.size, rightParts.size)
    for (index in 0 until size) {
        val delta = leftParts.getOrElse(index) { 0 } - rightParts.getOrElse(index) { 0 }
        if (delta != 0) return delta
    }
    return 0
}

internal fun xrayAndroidAssetName(abi: String): String = "Xray-android-$abi.zip"

internal fun normalizeXrayCoreVersion(version: String): String {
    val raw = version.trim()
    if (raw.isEmpty() || versionParts(raw).isEmpty()) return raw
    return if (raw.startsWith("v", ignoreCase = true)) raw else "v$raw"
}

private fun versionParts(version: String): List<Int> {
    return version
        .trim()
        .removePrefix("v")
        .removePrefix("V")
        .substringBefore('-')
        .substringBefore('+')
        .split('.')
        .mapNotNull { part -> part.toIntOrNull() }
}

private val XrayVersionPattern = Regex("""(?im)^\s*Xray\s+(v?\d+(?:\.\d+)*)""")
private val EmbeddedXrayVersionPattern = Regex("""Xray[\s/-]*(v?\d+\.\d+\.\d+)""", RegexOption.IGNORE_CASE)
private val GoModuleVersionPattern = Regex("""xtls/xray-core[\x00\s/]*v?(\d+\.\d+\.\d+)""", RegexOption.IGNORE_CASE)
private val FileNameVersionPattern = Regex("""(?i)(?:^|[^A-Za-z0-9])(v?\d{1,2}\.\d{1,2}\.\d{1,2})(?![0-9])""")
private val DownloadTagPattern = Regex("""/releases/download/(v?[\d.]+)/""")
private const val ReleasesApiUrl = "https://api.github.com/repos/XTLS/Xray-core/releases?per_page=40"
internal const val CustomXrayCoreVersion = "custom"
