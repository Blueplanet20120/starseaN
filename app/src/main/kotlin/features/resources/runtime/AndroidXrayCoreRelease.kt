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
        val errors = mutableListOf<Throwable>()
        listOf(
            { fetchLatestFromApi(proxy) },
            { fetchLatestFromAtom(proxy) },
            { fetchLatestFromLatestPage(proxy) },
        ).forEach { source ->
            try {
                return source()
            } catch (error: Throwable) {
                if (error is AndroidResourceFileDownloadCancelledException) throw error
                errors += error
                AndroidResourceFileLogger.info(
                    "AndroidLibXrayLite check via fallback after ${error.message}",
                )
            }
        }
        throw errors.lastOrNull() ?: error("AndroidLibXrayLite release check failed")
    }

    private fun fetchLatestFromApi(proxy: AndroidResourceFileDownloadProxy?): XrayCoreRelease {
        val payload = downloader.fetchText(
            url = ReleasesApiUrl,
            proxy = proxy,
            extraHeaders = mapOf(
                "Accept" to "application/vnd.github+json",
                "X-GitHub-Api-Version" to "2022-11-28",
            ),
        )
        val newest = pickNewestLiteRelease(payload)
        AndroidResourceFileLogger.info("AndroidLibXrayLite latest api tag=${newest.tag} aar=${newest.assetName}")
        return newest
    }

    private fun fetchLatestFromAtom(proxy: AndroidResourceFileDownloadProxy?): XrayCoreRelease {
        val payload = downloader.fetchText(url = ReleasesAtomUrl, proxy = proxy)
        val newest = pickNewestLiteReleaseFromAtom(payload)
        AndroidResourceFileLogger.info("AndroidLibXrayLite latest atom tag=${newest.tag}")
        return newest
    }

    private fun fetchLatestFromLatestPage(proxy: AndroidResourceFileDownloadProxy?): XrayCoreRelease {
        val hops = downloader.resolveRedirectChain(ReleasesLatestUrl, proxy)
        val tagUrl = hops.lastOrNull().orEmpty()
        val tag = tagUrl.substringAfterLast("/tag/").substringBefore('/').trim()
        require(tag.isNotEmpty() && tag != tagUrl) { "Cannot parse AndroidLibXrayLite latest tag" }
        val newest = liteRelease(tag)
        AndroidResourceFileLogger.info("AndroidLibXrayLite latest page tag=${newest.tag}")
        return newest
    }
}

internal fun pickNewestLiteRelease(payload: String): XrayCoreRelease {
    val trimmed = payload.trim()
    require(trimmed.startsWith("[")) { "Unexpected AndroidLibXrayLite releases response" }
    val releases = JSONArray(trimmed)
    var newest: XrayCoreRelease? = null
    for (index in 0 until releases.length()) {
        val json = releases.optJSONObject(index) ?: continue
        if (json.optBoolean("draft")) continue
        val release = parseLiteRelease(json) ?: continue
        if (newest == null || compareXrayCoreVersion(release.tag, newest.tag) > 0) {
            newest = release
        }
    }
    return newest ?: error("No libv2ray.aar in recent AndroidLibXrayLite releases")
}

internal fun parseLiteRelease(json: JSONObject): XrayCoreRelease? {
    val tag = json.optString("tag_name").trim()
    if (tag.isEmpty()) return null
    val assets = json.optJSONArray("assets") ?: return null
    var downloadUrl = ""
    for (index in 0 until assets.length()) {
        val asset = assets.optJSONObject(index) ?: continue
        if (asset.optString("name").equals("libv2ray.aar", ignoreCase = true)) {
            downloadUrl = asset.optString("browser_download_url").trim()
            break
        }
    }
    if (downloadUrl.isEmpty()) return null
    return liteRelease(
        tag = tag,
        title = json.optString("name").trim().ifEmpty { "AndroidLibXrayLite $tag" },
        body = json.optString("body").trim(),
        htmlUrl = json.optString("html_url").trim(),
        downloadUrl = downloadUrl,
    )
}

internal fun pickNewestLiteReleaseFromAtom(payload: String): XrayCoreRelease {
    val tags = LiteReleaseTagInUrlPattern.findAll(payload)
        .map { match -> match.groupValues[1].trim() }
        .filter { tag -> tag.isNotEmpty() }
        .distinct()
        .toList()
    val newestTag = tags.maxWithOrNull { left, right -> compareXrayCoreVersion(left, right) }
        ?: error("No AndroidLibXrayLite tags in releases.atom")
    return liteRelease(newestTag)
}

internal fun liteRelease(
    tag: String,
    title: String = "AndroidLibXrayLite $tag",
    body: String = "",
    htmlUrl: String = "https://github.com/$LiteOwnerRepo/releases/tag/$tag",
    downloadUrl: String = "https://github.com/$LiteOwnerRepo/releases/download/$tag/libv2ray.aar",
): XrayCoreRelease {
    return XrayCoreRelease(
        tag = tag,
        title = title,
        body = body,
        htmlUrl = htmlUrl,
        assetName = "libv2ray.aar",
        downloadUrl = downloadUrl,
    )
}

internal fun probeXrayCoreVersion(binary: File, allowExecute: Boolean = true): String? {
    if (!binary.isFile || binary.length() <= 0L) return null
    val scanned = scanXrayCoreVersion(binary)
    if (scanned != null) {
        AndroidResourceFileLogger.info("Xray-core scanned ${binary.name}=$scanned size=${binary.length()}")
        return scanned
    }
    if (!allowExecute) return null
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
    val found = mutableSetOf<String>()
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
            found += xrayCoreVersionsFromBinaryText(text)
            prefix = chunk.copyOfRange((chunk.size - overlap).coerceAtLeast(0), chunk.size)
        }
    }
    return found
        .filter(::isUserFacingXrayReleaseVersion)
        .maxWithOrNull { left, right -> compareXrayCoreVersion(left, right) }
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
    val version = normalizeXrayCoreVersion(match.groupValues[1])
    return version.takeIf(::isUserFacingXrayReleaseVersion)
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
    return xrayCoreVersionsFromBinaryText(text)
        .filter(::isUserFacingXrayReleaseVersion)
        .maxWithOrNull { left, right -> compareXrayCoreVersion(left, right) }
}

internal fun xrayCoreVersionsFromBinaryText(text: String): List<String> {
    return buildList {
        EmbeddedXrayVersionPattern.findAll(text).forEach { match ->
            add(normalizeXrayCoreVersion(match.groupValues[1]))
        }
        QuotedReleaseVersionPattern.findAll(text).forEach { match ->
            add(normalizeXrayCoreVersion(match.groupValues[1]))
        }
    }
}

internal fun isUserFacingXrayReleaseVersion(version: String): Boolean {
    val parts = versionParts(version)
    if (parts.size < 3) return false
    val major = parts[0]
    val minor = parts[1]
    if (major == 1 && minor >= 1_000) return false
    if (major == 1 && minor in 0..99) return true
    if (major in 24..29 && minor in 0..12) return true
    return false
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

internal fun xrayAndroidJniEntry(abi: String, name: String): String = "jni/$abi/$name"

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
private val QuotedReleaseVersionPattern = Regex(""""(v?(?:1\.\d{1,2}\.\d{1,2}|2[4-9]\.\d{1,2}\.\d{1,2}))"""")
private val FileNameVersionPattern = Regex("""(?i)(?:^|[^A-Za-z0-9])(v?\d{1,2}\.\d{1,2}\.\d{1,2})(?![0-9])""")
private val DownloadTagPattern = Regex("""/releases/download/(v?[\d.]+)/""")
private const val LiteOwnerRepo = "Blueplanet20120/AndroidLibXrayLite"
private const val ReleasesApiUrl =
    "https://api.github.com/repos/$LiteOwnerRepo/releases?per_page=40"
private const val ReleasesAtomUrl = "https://github.com/$LiteOwnerRepo/releases.atom"
private const val ReleasesLatestUrl = "https://github.com/$LiteOwnerRepo/releases/latest"
private val LiteReleaseTagInUrlPattern =
    Regex("""/Blueplanet20120/AndroidLibXrayLite/releases/tag/(v?[^\s"'<>/]+)""")
internal const val CustomXrayCoreVersion = "custom"

internal fun isPlaceholderNativeLibraryTimestamp(millis: Long): Boolean {
    return millis < Year2000UtcMillis
}

private const val Year2000UtcMillis = 946_684_800_000L
