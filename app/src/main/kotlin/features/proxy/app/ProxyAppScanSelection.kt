// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package features.proxy.app

import features.proxy.app.model.AppPackageEntry

/**
 * Replace the current per-app selection with preset whitelist packages.
 * Installed matches keep their userId keys; missing packages still get a key per user space
 * so they show as checked once they appear in the list.
 */
internal fun applyPresetProxyAppSelection(
    installedApps: List<AppPackageEntry>,
    userIds: Collection<Int>,
    presetPackageNames: Collection<String>,
    keyGroups: Collection<Collection<String>>,
): List<String> {
    val installedByPackage = installedApps.groupBy { app -> app.packageName }
    val fallbackUserIds = userIds.ifEmpty { listOf(0) }
    val keys = LinkedHashSet<String>()
    for (packageName in presetPackageNames) {
        val pkg = packageName.trim()
        if (pkg.isEmpty()) continue
        val installed = installedByPackage[pkg]
        if (!installed.isNullOrEmpty()) {
            installed.forEach { app ->
                keys.add("${app.userId ?: 0}:${app.packageName}")
            }
        } else {
            fallbackUserIds.forEach { userId ->
                keys.add("$userId:$pkg")
            }
        }
    }
    return expandSelectionToSharedUids(keys, keyGroups)
}

/**
 * Blacklist mode: append matched entries so the corresponding apps bypass the proxy.
 * Existing selection order is preserved; new matches are appended in scan order.
 */
internal fun mergeSelectedAppsForScan(
    current: List<String>,
    matched: Collection<String>,
): List<String> {
    if (matched.isEmpty()) return current
    val existing = LinkedHashSet(current)
    matched.forEach(existing::add)
    return existing.toList()
}

/**
 * Whitelist mode: invert selection w.r.t. the full installed set minus matched.
 * Result = (allInstalledKeys - matched) so every non-Chinese app gets proxied
 * while Chinese apps fall back to the default-deny (direct) path.
 */
internal fun invertSelectionForScan(
    matched: Collection<String>,
    allKeys: Collection<String>,
): List<String> {
    val matchedSet = matched.toSet()
    return allKeys.filterNot { it in matchedSet }.distinct()
}


/** Packages sharing a UID must be selected together because routing is UID-based. */
internal fun expandSelectionToSharedUids(
    selected: Collection<String>,
    keyGroups: Collection<Collection<String>>,
): List<String> {
    val selectedSet = selected.toSet()
    return keyGroups.filter { group -> group.any { it in selectedSet } }.flatten().distinct()
}
