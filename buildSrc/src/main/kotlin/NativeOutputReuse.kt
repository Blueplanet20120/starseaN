// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

import org.gradle.api.Project
import java.io.File

internal object NativeOutputReuse {
    fun isCurrent(output: File, sources: List<File>): Boolean {
        if (!output.isFile || output.length() <= 0L) {
            return false
        }
        val outputTime = output.lastModified()
        return sources.all { source -> newestFileTime(source) <= outputTime }
    }

    private fun newestFileTime(path: File): Long {
        if (!path.exists()) {
            return 0L
        }
        if (path.isFile) {
            return path.lastModified()
        }
        var newest = 0L
        path.walkTopDown()
            .onEnter { directory -> directory.name != ".git" }
            .filter(File::isFile)
            .forEach { file ->
                val time = file.lastModified()
                if (time > newest) {
                    newest = time
                }
            }
        return newest
    }
}

fun Project.nativeAndroidAbis(): List<String> {
    val override = findProperty("starsean.nativeAbis")?.toString()?.trim().orEmpty()
    if (override.isEmpty()) {
        return ProjectConfig.SUPPORTED_ANDROID_ABIS
    }
    val allowed = ProjectConfig.SUPPORTED_ANDROID_ABIS.toSet()
    val filtered = override.split(',')
        .map { abi -> abi.trim() }
        .filter { abi -> abi in allowed }
    check(filtered.isNotEmpty()) {
        "starsean.nativeAbis has no supported ABIs: $override"
    }
    return filtered
}
