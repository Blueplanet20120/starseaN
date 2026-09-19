// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package features.resources.runtime

import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import app.ProjectInfo
import app.R
import app.CustomResourceFileState
import app.ResourceFileKind
import app.ResourceFilesStatus
import app.ResourceFileUpdateSource
import app.modes.isRootRunMode
import engine.proxy.LocalProxyLoopbackAddress
import engine.proxy.LocalProxyRuntime
import engine.vpn.AndroidLibXrayLiteRuntime
import engine.network.isPort
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import features.resources.ResourceFileUpdateOptions
import engine.root.publication.RootCoreRemovalCommand
import system.AndroidRootShellGateway
import system.RootShellGateway
import system.ShellExecOptions

internal class AndroidResourceFileRepository(
    context: Context,
    private val currentRunMode: () -> Int,
    private val rootShell: RootShellGateway = AndroidRootShellGateway(),
) {
    private val appContext = context.applicationContext
    private val store = AndroidResourceFileStore(appContext)
    private val downloader = AndroidResourceFileDownloader()
    private val versionStore = AndroidXrayCoreVersionStore(appContext)
    private val xrayCoreReleaseClient = AndroidXrayCoreReleaseClient(downloader)

    suspend fun status(customResourceFiles: List<CustomResourceFileState> = emptyList()): ResourceFilesStatus =
        withContext(Dispatchers.IO) {
            presentedStatus(customResourceFiles)
        }

    suspend fun hasCustomXrayCore(): Boolean = withContext(Dispatchers.IO) {
        store.hasCustomXrayCore()
    }

    private suspend fun removeCustomXrayCore() {
        val target = store.file(ResourceFileKind.XrayCore)
        sharedCoreReplacementCoordinator.execute(
            targetOwnerUid = target::coreBinaryOwnerUidOrNull,
            rootModeActive = { currentRunMode().isRootRunMode() },
            candidateFactory = { },
            installInitial = {},
            replaceAppOwned = {
                check(target.delete()) { "Failed to remove the custom Xray core" }
                store.removeLiteCompanionLibrary()
            },
            replaceWithRoot = {
                val result = rootShell.exec(
                    RootCoreRemovalCommand.build(target.absolutePath),
                    ShellExecOptions(logFailure = false),
                )
                check(result.errno == 0) { result.stderr.ifBlank { "Failed to remove the custom Xray core" } }
                store.removeLiteCompanionLibrary()
            },
            deferRootOwned = { error(appContext.getString(R.string.settings_root_required)) },
        )
    }

    suspend fun synchronizeBundledFilesAfterPackageUpdate(resourceFileSource: Int) {
        withContext(Dispatchers.IO) {
            store.synchronizeBundledFilesAfterPackageUpdate(resourceFileSource)
            adoptPackagedXrayCoreIfNewer()
        }
    }

    private suspend fun adoptPackagedXrayCoreIfNewer() {
        val packaged = ProjectInfo.ANDROID_LIB_XRAY_LITE_VERSION
        val installed = versionStore.installedVersion()
        if (installed.equals(CustomXrayCoreVersion, ignoreCase = true)) {
            return
        }
        if (!isNewerXrayCoreVersion(packaged, installed)) {
            return
        }
        if (store.hasCustomXrayCore()) {
            runCatching { removeCustomXrayCore() }
                .onFailure { error ->
                    AndroidResourceFileLogger.warn(
                        "Packaged AndroidLibXrayLite $packaged is newer than $installed, but custom core was kept",
                        error,
                    )
                    return
                }
        }
        versionStore.setInstalledVersion(normalizeXrayCoreVersion(packaged))
        versionStore.touchUpdatedAt(appContext.packageUpdatedAtMillis())
        AndroidResourceFileLogger.info(
            "Adopted packaged AndroidLibXrayLite $packaged over remembered $installed",
        )
    }

    suspend fun deleteCustom(
        customFile: CustomResourceFileState,
        customResourceFiles: List<CustomResourceFileState>,
    ): ResourceFilesStatus = withContext(Dispatchers.IO) {
        store.deleteCustom(customFile)
        presentedStatus(customResourceFiles)
    }

    suspend fun renameCustom(
        previousFile: CustomResourceFileState,
        customFile: CustomResourceFileState,
        customResourceFiles: List<CustomResourceFileState>,
    ): ResourceFilesStatus = withContext(Dispatchers.IO) {
        store.renameCustom(previousFile, customFile)
        presentedStatus(customResourceFiles)
    }

    suspend fun update(
        source: ResourceFileUpdateSource,
        options: ResourceFileUpdateOptions,
        customResourceFiles: List<CustomResourceFileState> = emptyList(),
    ): ResourceFilesStatus = withContext(Dispatchers.IO) {
        updateTargets(
            downloads = UpdateableResourceFileKinds.map { kind ->
                kind.toDownloadTarget(source)
            } + customResourceFiles.mapNotNull { customFile -> customFile.toDownloadTargetOrNull() },
            options = options,
            customResourceFiles = customResourceFiles,
        )
    }

    suspend fun update(
        kind: ResourceFileKind,
        source: ResourceFileUpdateSource,
        options: ResourceFileUpdateOptions,
        customResourceFiles: List<CustomResourceFileState> = emptyList(),
    ): ResourceFilesStatus = withContext(Dispatchers.IO) {
        updateTargets(
            downloads = listOf(kind.toDownloadTarget(source)),
            options = options,
            customResourceFiles = customResourceFiles,
        )
    }

    suspend fun updateCustom(
        customFile: CustomResourceFileState,
        options: ResourceFileUpdateOptions,
        customResourceFiles: List<CustomResourceFileState> = emptyList(),
    ): ResourceFilesStatus = withContext(Dispatchers.IO) {
        updateTargets(
            downloads = listOfNotNull(customFile.toDownloadTargetOrNull()),
            options = options,
            customResourceFiles = customResourceFiles,
        )
    }

    fun installedXrayCoreVersion(): String {
        if (!store.hasCustomXrayCore()) {
            return ProjectInfo.ANDROID_LIB_XRAY_LITE_VERSION
        }
        return versionStore.installedVersion()
    }

    suspend fun refreshInstalledXrayCoreVersion(): String {
        if (!store.hasCustomXrayCore()) {
            versionStore.setInstalledVersion(ProjectInfo.ANDROID_LIB_XRAY_LITE_VERSION)
            return ProjectInfo.ANDROID_LIB_XRAY_LITE_VERSION
        }
        rememberInstalledXrayCoreVersion()
        return versionStore.installedVersion()
    }

    suspend fun checkXrayCoreRelease(
        options: ResourceFileUpdateOptions,
    ): XrayCoreReleaseCheck = withContext(Dispatchers.IO) {
        val installed = refreshInstalledXrayCoreVersion()
        val latest = xrayCoreReleaseClient.fetchLatest(options.toDownloadProxy())
        val newer = isNewerXrayCoreVersion(latest.tag, installed)
        AndroidResourceFileLogger.info(
            "AndroidLibXrayLite check installed=$installed latest=${latest.tag} newer=$newer",
        )
        XrayCoreReleaseCheck(
            installedVersion = installed,
            latest = latest,
            isNewer = newer,
        )
    }

    suspend fun updateXrayCore(
        version: String,
        downloadUrl: String,
        options: ResourceFileUpdateOptions,
        customResourceFiles: List<CustomResourceFileState> = emptyList(),
    ): ResourceFilesStatus = withContext(Dispatchers.IO) {
        AndroidResourceFileDownloadCancellation.begin()
        currentCoroutineContext().ensureActive()
        val notifier = AndroidResourceFileDownloadNotifier(appContext)
        val downloadProxy = options.toDownloadProxy()
        if (downloadProxy != null) {
            AndroidResourceFileLogger.info(
                "Xray-core update will use local proxy ${downloadProxy.host}:${downloadProxy.port}",
            )
        }
        val zipFile = java.io.File.createTempFile("libv2ray-", ".aar", appContext.cacheDir)
        val result = runCatching {
            notifier.showProgress("AndroidLibXrayLite $version", progress = null, force = true)
            downloader.download(downloadUrl, zipFile, downloadProxy) { downloadedBytes, totalBytes ->
                notifier.showProgress(
                    fileName = "AndroidLibXrayLite $version",
                    progress = overallProgress(
                        fileIndex = 0,
                        fileCount = 1,
                        downloadedBytes = downloadedBytes,
                        totalBytes = totalBytes,
                    ),
                )
            }
            val candidate = store.stageXrayCoreCandidateFromAar(zipFile)
            installOrPublishCoreCandidate(
                candidateFactory = { candidate },
                knownVersion = version,
            )
            presentedStatus(customResourceFiles)
        }
        zipFile.delete()
        result.onSuccess {
            runCatching { notifier.showComplete() }
        }.onFailure { error ->
            if (error is AndroidResourceFileDownloadCancelledException) {
                AndroidResourceFileLogger.info("Xray-core update cancelled")
                runCatching { notifier.showCancelled() }
            } else {
                AndroidResourceFileLogger.error("Failed to update Xray-core", error)
                runCatching { notifier.showFailed(error.message ?: error::class.simpleName.orEmpty()) }
            }
        }
        result.getOrElse { error ->
            if (error is AndroidResourceFileDownloadCancelledException) {
                throw AndroidResourceFileDownloadCancelledException(
                    appContext.getString(R.string.resource_file_download_notification_cancelled),
                )
            }
            throw error
        }
    }

    private suspend fun updateTargets(
        downloads: List<ResourceFileDownloadTarget>,
        options: ResourceFileUpdateOptions,
        customResourceFiles: List<CustomResourceFileState>,
    ): ResourceFilesStatus {
        if (downloads.isEmpty()) {
            return presentedStatus(customResourceFiles)
        }
        store.dataDir.mkdirs()
        AndroidResourceFileDownloadCancellation.begin()
        currentCoroutineContext().ensureActive()
        val notifier = AndroidResourceFileDownloadNotifier(appContext)
        val downloadProxy = options.toDownloadProxy()
        if (downloadProxy != null) {
            AndroidResourceFileLogger.info(
                "Resource file update will use local proxy ${downloadProxy.host}:${downloadProxy.port}",
            )
        }
        val result = runCatching {
            downloads.forEachIndexed { index, download ->
                try {
                    notifier.showProgress(download.displayName, progress = null, force = true)
                    downloader.download(download.url, download.targetFile, downloadProxy) { downloadedBytes, totalBytes ->
                        notifier.showProgress(
                            fileName = download.displayName,
                            progress = overallProgress(
                                fileIndex = index,
                                fileCount = downloads.size,
                                downloadedBytes = downloadedBytes,
                                totalBytes = totalBytes,
                            ),
                        )
                    }
                    download.applyPermissions()
                } catch (error: Throwable) {
                    if (error is AndroidResourceFileDownloadCancelledException) throw error
                    throw ResourceFileDownloadFailedException(download.displayName, error)
                }
            }
            presentedStatus(customResourceFiles)
        }
        result.onSuccess {
            runCatching { notifier.showComplete() }
        }.onFailure { error ->
            if (error is AndroidResourceFileDownloadCancelledException) {
                AndroidResourceFileLogger.info("Resource file update cancelled")
                runCatching { notifier.showCancelled() }
            } else {
                AndroidResourceFileLogger.error("Failed to update resource files", error)
                runCatching { notifier.showFailed(error.message ?: error::class.simpleName.orEmpty()) }
            }
        }
        return result.getOrElse { error ->
            if (error is AndroidResourceFileDownloadCancelledException) {
                throw AndroidResourceFileDownloadCancelledException(
                    appContext.getString(R.string.resource_file_download_notification_cancelled),
                )
            }
            throw error
        }
    }

    private fun CustomResourceFileState.toDownloadTargetOrNull(): ResourceFileDownloadTarget? {
        val target = store.file(this)
        if (ResourceFileKind.entries.any { kind -> kind.fileName == target.name }) return null
        val updateUrl = url.trim()
        if (updateUrl.isBlank()) return null
        return ResourceFileDownloadTarget(
            displayName = name,
            url = updateUrl,
            targetFile = target,
        )
    }

    private fun ResourceFileKind.toDownloadTarget(source: ResourceFileUpdateSource): ResourceFileDownloadTarget {
        val updateUrl = when (this) {
            ResourceFileKind.GeoIp -> source.geoIpUrl
            ResourceFileKind.GeoSite -> source.geoSiteUrl
            ResourceFileKind.GeoIpOnlyCnPrivate -> source.geoIpOnlyCnPrivateUrl
            ResourceFileKind.DirectCidrIpv4 -> source.directCidrIpv4Url
            ResourceFileKind.DirectCidrIpv6 -> source.directCidrIpv6Url
            ResourceFileKind.XrayCore -> error("${ResourceFileKind.XrayCore.displayName} cannot be updated from URL")
        }
        return ResourceFileDownloadTarget(
            displayName = displayName,
            url = updateUrl,
            targetFile = store.file(this),
            applyPermissions = { store.applyPermissions(this) },
        )
    }

    suspend fun replaceCustom(
        customFile: CustomResourceFileState,
        uri: Uri,
        customResourceFiles: List<CustomResourceFileState> = emptyList(),
    ): ResourceFilesStatus = withContext(Dispatchers.IO) {
        store.replaceCustom(customFile, uri)
        presentedStatus(customResourceFiles)
    }

    suspend fun replace(
        kind: ResourceFileKind,
        uri: Uri,
        customResourceFiles: List<CustomResourceFileState> = emptyList(),
    ): ResourceFilesStatus = withContext(Dispatchers.IO) {
        if (kind == ResourceFileKind.XrayCore) {
            installOrPublishCoreCandidate(
                candidateFactory = { store.stageXrayCoreCandidate(uri) },
                knownVersion = xrayCoreVersionFromUri(uri) ?: CustomXrayCoreVersion,
            )
        } else {
            store.replace(kind, uri)
        }
        presentedStatus(customResourceFiles)
    }

    suspend fun restoreBundled(
        kind: ResourceFileKind,
        customResourceFiles: List<CustomResourceFileState> = emptyList(),
    ): ResourceFilesStatus = withContext(Dispatchers.IO) {
        if (kind == ResourceFileKind.XrayCore) {
            removeCustomXrayCore()
            versionStore.setInstalledVersion(ProjectInfo.ANDROID_LIB_XRAY_LITE_VERSION)
            versionStore.touchUpdatedAt()
        } else {
            store.restoreBundled(kind)
        }
        presentedStatus(customResourceFiles)
    }

    private suspend fun installOrPublishCoreCandidate(
        candidateFactory: () -> java.io.File,
        knownVersion: String? = null,
    ) {
        executeCoreCandidateInstall(candidateFactory, knownVersion) {
            error(appContext.getString(R.string.settings_root_required))
        }
    }

    private suspend fun executeCoreCandidateInstall(
        candidateFactory: () -> java.io.File,
        knownVersion: String? = null,
        deferRootOwned: suspend () -> Unit,
    ) {
        val target = store.file(ResourceFileKind.XrayCore)
        sharedCoreReplacementCoordinator.execute(
            targetOwnerUid = target::coreBinaryOwnerUidOrNull,
            rootModeActive = { currentRunMode().isRootRunMode() },
            candidateFactory = candidateFactory,
            installInitial = { candidate -> installInitialCoreCandidate(candidate, knownVersion) },
            replaceAppOwned = { candidate -> replaceAppOwnedCoreCandidate(candidate, knownVersion) },
            replaceWithRoot = { candidate -> replaceCoreCandidateWithRoot(candidate, knownVersion) },
            deferRootOwned = deferRootOwned,
        )
    }

    private suspend fun installInitialCoreCandidate(
        candidate: java.io.File,
        knownVersion: String? = null,
    ) {
        try {
            val installed = store.installInitialXrayCoreCandidate(candidate)
            require(installed || store.file(ResourceFileKind.XrayCore).isFile) {
                "Failed to install the initial Xray core"
            }
            rememberInstalledXrayCoreVersion(
                candidate = candidate,
                knownVersion = knownVersion,
                recordOperationTime = true,
            )
        } finally {
            candidate.delete()
        }
    }

    private suspend fun replaceAppOwnedCoreCandidate(
        candidate: java.io.File,
        knownVersion: String? = null,
    ) {
        try {
            store.replaceXrayCoreCandidate(candidate)
            rememberInstalledXrayCoreVersion(
                candidate = candidate,
                knownVersion = knownVersion,
                recordOperationTime = true,
            )
        } finally {
            candidate.delete()
        }
    }

    private suspend fun replaceCoreCandidateWithRoot(
        candidate: java.io.File,
        knownVersion: String? = null,
    ) {
        try {
            val target = store.file(ResourceFileKind.XrayCore)
            val removal = rootShell.exec(
                RootCoreRemovalCommand.build(target.absolutePath),
                ShellExecOptions(logFailure = false),
            )
            if (removal.errno != 0) {
                error(removal.stderr.ifBlank { "Failed to remove the existing Xray core" })
            }
            store.replaceXrayCoreCandidate(candidate)
            rememberInstalledXrayCoreVersion(
                candidate = candidate,
                knownVersion = knownVersion,
                recordOperationTime = true,
            )
        } finally {
            candidate.delete()
        }
    }

    private fun presentedStatus(
        customResourceFiles: List<CustomResourceFileState> = emptyList(),
    ): ResourceFilesStatus {
        val status = store.currentStatus(customResourceFiles)
        return status.copy(
            xrayCore = status.xrayCore.copy(
                updatedAtMillis = resolveXrayCoreUpdatedAt(status.xrayCore.updatedAtMillis),
                kernelVersion = resolveXrayKernelVersion(),
            ),
        )
    }

    private fun resolveXrayKernelVersion(): String {
        val gojni = store.effectiveXrayGoJniFile()
        val launcher = store.effectiveXrayCoreFile()
        val fingerprint = listOf(gojni, launcher).joinToString("|") { file ->
            "${file.absolutePath}:${file.takeIf { it.isFile }?.lastModified() ?: 0}:${file.takeIf { it.isFile }?.length() ?: 0}:kernel-display-v3-checkversionx"
        }
        versionStore.kernelVersion(fingerprint)?.let { return it }
        val probed = AndroidLibXrayLiteRuntime.packagedXrayCoreVersion()
        if (probed != null) {
            versionStore.setKernelVersion(probed, fingerprint)
            AndroidResourceFileLogger.info("Xray kernel from Libv2ray.checkVersionX=$probed")
        }
        return probed.orEmpty()
    }

    private fun resolveXrayCoreUpdatedAt(fileUpdatedAtMillis: Long): Long {
        val recorded = versionStore.updatedAtMillis()
        if (recorded > 0L) return recorded
        if (fileUpdatedAtMillis <= 0L || isPlaceholderNativeLibraryTimestamp(fileUpdatedAtMillis)) {
            return appContext.packageUpdatedAtMillis()
        }
        return fileUpdatedAtMillis
    }

    private suspend fun rememberInstalledXrayCoreVersion(
        candidate: java.io.File? = null,
        knownVersion: String? = null,
        recordOperationTime: Boolean = false,
    ) {
        val installed = store.effectiveXrayCoreFile()
        val probed = candidate?.let(::probeXrayCoreVersion)
            ?: probeXrayCoreVersion(installed)
            ?: probeXrayCoreVersionWithShell(installed) { command ->
                val result = rootShell.exec(command, ShellExecOptions(logFailure = false))
                listOf(result.stdout, result.stderr)
                    .filter { it.isNotBlank() }
                    .joinToString("\n")
            }
        val version = probed ?: knownVersion?.trim()?.takeIf(String::isNotEmpty)
        if (version != null) {
            versionStore.setInstalledVersion(normalizeXrayCoreVersion(version))
            if (recordOperationTime) {
                versionStore.touchUpdatedAt()
            }
            AndroidResourceFileLogger.info(
                "Xray-core remembered version=$version probed=${probed != null}",
            )
        }
    }

    private fun xrayCoreVersionFromUri(uri: Uri): String? {
        val names = buildList {
            add(uri.lastPathSegment.orEmpty())
            add(uri.toString())
            runCatching {
                appContext.contentResolver.query(
                    uri,
                    arrayOf(OpenableColumns.DISPLAY_NAME),
                    null,
                    null,
                    null,
                )?.use { cursor ->
                    if (cursor.moveToFirst()) {
                        add(cursor.getString(0).orEmpty())
                    }
                }
            }
        }
        return names.firstNotNullOfOrNull(::xrayCoreVersionFromName)
    }
}

private data class ResourceFileDownloadTarget(
    val displayName: String,
    val url: String,
    val targetFile: java.io.File,
    val applyPermissions: () -> Unit = {},
)

private val UpdateableResourceFileKinds = listOf(
    ResourceFileKind.GeoIp,
    ResourceFileKind.GeoSite,
    ResourceFileKind.GeoIpOnlyCnPrivate,
    ResourceFileKind.DirectCidrIpv4,
    ResourceFileKind.DirectCidrIpv6,
)

private class ResourceFileDownloadFailedException(
    fileName: String,
    cause: Throwable,
) : RuntimeException("$fileName: ${cause.message ?: cause::class.simpleName.orEmpty()}", cause)

private fun ResourceFileUpdateOptions.toDownloadProxy(): AndroidResourceFileDownloadProxy? {
    if (!useRunningProxy) return null
    val runtimeOptions = LocalProxyRuntime.current()
    val port = runtimeOptions?.port
        ?: fallbackProxyPort?.takeIf(Int::isPort)
        ?: error("Local proxy port is unavailable")
    return AndroidResourceFileDownloadProxy(
        host = LocalProxyLoopbackAddress,
        port = port,
        username = runtimeOptions?.username ?: fallbackProxyUsername,
        password = runtimeOptions?.password ?: fallbackProxyPassword,
    )
}
