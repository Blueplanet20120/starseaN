// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

import org.gradle.api.DefaultTask
import org.gradle.api.Project
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.provider.Property
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.OutputDirectory
import org.gradle.api.tasks.TaskAction
import java.io.File
import java.util.Properties

object ProjectConfig {
    const val JVM_VERSION = 26
    const val PROJECT_NAME = "starseaN"
    const val PACKAGE_NAME = "org.starsean"
    const val STARSEAD_VERSION = "v2.0.32"
    const val BPF2SOCKS_VERSION = "v1.0.15"
    const val BPF_MATCHER_VERSION = "v1.0.1"
    const val ANDROID_LIB_XRAY_LITE_VERSION_FALLBACK = "v26.9.10"
    const val HEV_SOCKS5_TUNNEL_VERSION = "2.17.1"
    const val TARGET_SDK = 37
    const val MIN_SDK = 26
    val SUPPORTED_ANDROID_ABIS = listOf("arm64-v8a")
}

private const val AndroidLibXrayLiteOwnerRepo = "Blueplanet20120/AndroidLibXrayLite"
private const val AndroidLibXrayLiteReleasesApi =
    "https://api.github.com/repos/$AndroidLibXrayLiteOwnerRepo/releases/latest"
private const val AndroidLibXrayLiteReleasesAtom =
    "https://github.com/$AndroidLibXrayLiteOwnerRepo/releases.atom"
private const val AndroidLibXrayLiteReleasesLatest =
    "https://github.com/$AndroidLibXrayLiteOwnerRepo/releases/latest"

fun resolveAndroidLibXrayLiteVersion(project: Project? = null): String {
    val pinned = project?.findProperty("androidLibXrayLiteVersion")?.toString()?.trim().orEmpty()
    if (pinned.isNotEmpty()) {
        return pinned
    }
    return fetchLatestAndroidLibXrayLiteTag() ?: ProjectConfig.ANDROID_LIB_XRAY_LITE_VERSION_FALLBACK
}

fun fetchLatestAndroidLibXrayLiteTag(): String? {
    return fetchGithubJsonTag(AndroidLibXrayLiteReleasesApi)
        ?: fetchGithubAtomTag(AndroidLibXrayLiteReleasesAtom)
        ?: fetchGithubLatestRedirectTag(AndroidLibXrayLiteReleasesLatest)
}

private fun fetchGithubJsonTag(url: String): String? {
    val body = fetchUrlText(url, accept = "application/vnd.github+json") ?: return null
    return Regex("\"tag_name\"\\s*:\\s*\"([^\"]+)\"").find(body)?.groupValues?.get(1)?.trim()?.takeIf { it.isNotEmpty() }
}

private fun fetchGithubAtomTag(url: String): String? {
    val body = fetchUrlText(url) ?: return null
    val tags = Regex("/$AndroidLibXrayLiteOwnerRepo/releases/tag/(v?[^\\s\"'<>/]+)")
        .findAll(body)
        .map { it.groupValues[1].trim() }
        .filter { it.isNotEmpty() }
        .toList()
    return tags.maxWithOrNull { left, right -> compareLooseVersion(left, right) }
}

private fun fetchGithubLatestRedirectTag(url: String): String? {
    return try {
        val connection = java.net.URI(url).toURL().openConnection() as java.net.HttpURLConnection
        connection.instanceFollowRedirects = false
        connection.connectTimeout = 8_000
        connection.readTimeout = 8_000
        applyGithubHeaders(connection)
        connection.responseCode
        val location = connection.getHeaderField("Location").orEmpty()
        connection.disconnect()
        location.substringAfterLast("/tag/").substringBefore('/').trim().takeIf { it.isNotEmpty() && it != location }
    } catch (_: Exception) {
        null
    }
}

private fun githubToken(): String {
    return System.getenv("GITHUB_TOKEN")?.trim().orEmpty()
        .ifEmpty { System.getenv("GH_TOKEN")?.trim().orEmpty() }
}

private fun applyGithubHeaders(connection: java.net.HttpURLConnection, accept: String? = null) {
    connection.setRequestProperty("User-Agent", "starseaN-gradle")
    connection.setRequestProperty("X-GitHub-Api-Version", "2022-11-28")
    if (accept != null) {
        connection.setRequestProperty("Accept", accept)
    }
    val token = githubToken()
    if (token.isNotEmpty()) {
        connection.setRequestProperty("Authorization", "Bearer $token")
    }
}

private fun fetchUrlText(url: String, accept: String? = null): String? {
    return try {
        val connection = java.net.URI(url).toURL().openConnection() as java.net.HttpURLConnection
        connection.instanceFollowRedirects = true
        connection.connectTimeout = 8_000
        connection.readTimeout = 8_000
        applyGithubHeaders(connection, accept)
        val code = connection.responseCode
        if (code !in 200..299) {
            connection.disconnect()
            return null
        }
        connection.inputStream.bufferedReader().use { it.readText() }
    } catch (_: Exception) {
        null
    }
}

private fun compareLooseVersion(left: String, right: String): Int {
    fun parts(value: String): List<Int> {
        return value.trim().removePrefix("v").removePrefix("V")
            .substringBefore('-').substringBefore('+')
            .split('.').mapNotNull { it.toIntOrNull() }
    }
    val leftParts = parts(left)
    val rightParts = parts(right)
    val size = maxOf(leftParts.size, rightParts.size)
    for (index in 0 until size) {
        val delta = leftParts.getOrElse(index) { 0 } - rightParts.getOrElse(index) { 0 }
        if (delta != 0) return delta
    }
    return 0
}

fun loadAppVersionName(rootDir: File): String {
    val file = File(rootDir, "version.properties")
    check(file.isFile) { "Missing ${file.absolutePath}" }
    val properties = Properties()
    file.inputStream().use(properties::load)
    return properties.getProperty("VERSION_NAME")?.trim()?.takeIf(String::isNotEmpty)
        ?: error("VERSION_NAME missing in ${file.absolutePath}")
}

fun Project.appVersionName(): String = loadAppVersionName(rootProject.projectDir)

fun Project.appVersionCode(): Int = versionCodeFromName(appVersionName())

fun versionCodeFromName(versionName: String): Int {
    val core = versionName
        .trim()
        .removePrefix("v")
        .removePrefix("V")
        .substringBefore('-')
        .substringBefore('+')
    val parts = core.split('.').mapNotNull { it.toIntOrNull() }
    val major = parts.getOrElse(0) { 0 }
    val minor = parts.getOrElse(1) { 0 }
    val patch = parts.getOrElse(2) { 0 }
    return major * 10_000 + minor * 100 + patch
}

abstract class GenerateProjectInfoTask : DefaultTask() {
    @get:Input
    abstract val packageName: Property<String>

    @get:Input
    abstract val projectName: Property<String>

    @get:Input
    abstract val versionName: Property<String>

    @get:Input
    abstract val versionCode: Property<Int>

    @get:Input
    abstract val xrayCoreVersion: Property<String>

    @get:Input
    abstract val androidLibXrayLiteVersion: Property<String>

    @get:Input
    abstract val hevSocks5TunnelVersion: Property<String>

    @get:OutputDirectory
    abstract val outputDirectory: DirectoryProperty

    @TaskAction
    fun generate() {
        val packagePath = packageName.get().replace('.', '/')
        val file = outputDirectory.file("$packagePath/ProjectInfo.kt").get().asFile
        file.parentFile.mkdirs()
        file.writeText(
            """
            package ${packageName.get()}

            object ProjectInfo {
                const val PROJECT_NAME = "${projectName.get()}"
                const val VERSION_NAME = "${versionName.get()}"
                const val VERSION_CODE = ${versionCode.get()}
                const val XRAY_CORE_VERSION = "${xrayCoreVersion.get()}"
                const val ANDROID_LIB_XRAY_LITE_VERSION = "${androidLibXrayLiteVersion.get()}"
                const val HEV_SOCKS5_TUNNEL_VERSION = "${hevSocks5TunnelVersion.get()}"
            }
            """.trimIndent(),
        )
    }
}
