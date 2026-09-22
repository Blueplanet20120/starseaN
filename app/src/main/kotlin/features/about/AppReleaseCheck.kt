// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package features.about

import app.AppState
import engine.network.isPort
import engine.network.toPortOrNull
import engine.proxy.LocalProxyLoopbackAddress
import engine.proxy.LocalProxyRuntime
import features.resources.runtime.AndroidResourceFileDownloadCancelledException
import features.resources.runtime.AndroidResourceFileDownloadProxy
import features.resources.runtime.AndroidResourceFileDownloader
import org.json.JSONObject

internal data class AppRelease(
    val version: String,
    val title: String,
    val notes: String,
    val pageUrl: String,
    val downloadUrl: String,
)

internal data class AppReleaseCheck(
    val installedVersion: String,
    val latest: AppRelease,
    val isNewer: Boolean,
)

internal class AppReleaseClient(
    private val downloader: AndroidResourceFileDownloader = AndroidResourceFileDownloader(),
) {
    fun check(installedVersion: String, proxy: AndroidResourceFileDownloadProxy?): AppReleaseCheck {
        val latest = fetchLatest(proxy)
        return AppReleaseCheck(
            installedVersion = installedVersion,
            latest = latest,
            isNewer = isNewerAppVersion(latest.version, installedVersion),
        )
    }

    private fun fetchLatest(proxy: AndroidResourceFileDownloadProxy?): AppRelease {
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
            }
        }
        throw errors.lastOrNull() ?: error("starseaN release check failed")
    }

    private fun fetchLatestFromApi(proxy: AndroidResourceFileDownloadProxy?): AppRelease {
        val payload = downloader.fetchText(
            url = ReleasesApiUrl,
            proxy = proxy,
            extraHeaders = mapOf(
                "Accept" to "application/vnd.github+json",
                "X-GitHub-Api-Version" to "2022-11-28",
            ),
        )
        return parseLatestRelease(payload)
    }

    private fun fetchLatestFromAtom(proxy: AndroidResourceFileDownloadProxy?): AppRelease {
        val payload = downloader.fetchText(url = ReleasesAtomUrl, proxy = proxy)
        val tag = AtomTagPattern.find(payload)?.groupValues?.getOrNull(1)?.trim().orEmpty()
        require(tag.isNotEmpty()) { "Cannot parse starseaN release feed" }
        return releaseFromTag(tag)
    }

    private fun fetchLatestFromLatestPage(proxy: AndroidResourceFileDownloadProxy?): AppRelease {
        val hops = downloader.resolveRedirectChain(ReleasesLatestUrl, proxy)
        val tag = hops.lastOrNull().orEmpty().substringAfterLast("/tag/").substringBefore('/').trim()
        require(tag.isNotEmpty() && !tag.contains("://")) { "Cannot parse starseaN latest tag" }
        return releaseFromTag(tag)
    }
}

internal fun AppState.appReleaseProxy(): AndroidResourceFileDownloadProxy? {
    if (!enableResourceUpdateViaProxy || !proxyRunning) return null
    val runtime = LocalProxyRuntime.current()
    val port = runtime?.port ?: localProxyPort.toPortOrNull()?.takeIf(Int::isPort) ?: return null
    return AndroidResourceFileDownloadProxy(
        host = LocalProxyLoopbackAddress,
        port = port,
        username = runtime?.username ?: localProxyUsername,
        password = runtime?.password ?: localProxyPassword,
    )
}

internal fun parseLatestRelease(payload: String): AppRelease {
    val json = JSONObject(payload.trim())
    val tag = json.optString("tag_name").trim()
    require(tag.isNotEmpty()) { "starseaN release has no tag" }
    val assets = json.optJSONArray("assets")
    var downloadUrl = ""
    if (assets != null) {
        var fallbackApk = ""
        for (index in 0 until assets.length()) {
            val asset = assets.optJSONObject(index) ?: continue
            val name = asset.optString("name")
            if (!name.endsWith(".apk", ignoreCase = true)) continue
            val url = asset.optString("browser_download_url").trim()
            if (url.isEmpty()) continue
            if (fallbackApk.isEmpty()) fallbackApk = url
            if (name.contains("arm64-v8a", ignoreCase = true)) {
                downloadUrl = url
                break
            }
        }
        if (downloadUrl.isEmpty()) downloadUrl = fallbackApk
    }
    val pageUrl = json.optString("html_url").trim().ifEmpty { releasePageUrl(tag) }
    return AppRelease(
        version = displayAppVersion(tag),
        title = json.optString("name").trim().ifEmpty { "starseaN $tag" },
        notes = json.optString("body").trim(),
        pageUrl = pageUrl,
        downloadUrl = downloadUrl,
    )
}

internal fun isNewerAppVersion(latest: String, installed: String): Boolean {
    val latestParts = versionParts(latest)
    val installedParts = versionParts(installed)
    if (latestParts.isEmpty()) return false
    if (installedParts.isEmpty()) return true
    val size = maxOf(latestParts.size, installedParts.size)
    for (index in 0 until size) {
        val delta = latestParts.getOrElse(index) { 0 } - installedParts.getOrElse(index) { 0 }
        if (delta != 0) return delta > 0
    }
    return false
}

internal fun displayAppVersion(version: String): String {
    val trimmed = version.trim().removePrefix("v").removePrefix("V")
    return "v$trimmed"
}

private fun releaseFromTag(tag: String): AppRelease {
    val version = displayAppVersion(tag)
    return AppRelease(
        version = version,
        title = "starseaN $version",
        notes = "",
        pageUrl = releasePageUrl(tag),
        downloadUrl = "",
    )
}

private fun releasePageUrl(tag: String): String {
    val normalized = tag.trim().let { value ->
        if (value.startsWith("v", ignoreCase = true)) value else "v$value"
    }
    return "https://github.com/$OwnerRepo/releases/tag/$normalized"
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

private const val OwnerRepo = "Blueplanet20120/starseaN"
private const val ReleasesApiUrl = "https://api.github.com/repos/$OwnerRepo/releases/latest"
private const val ReleasesAtomUrl = "https://github.com/$OwnerRepo/releases.atom"
private const val ReleasesLatestUrl = "https://github.com/$OwnerRepo/releases/latest"
private val AtomTagPattern = Regex("""/releases/tag/(v?[^<&"'\s/]+)""")
