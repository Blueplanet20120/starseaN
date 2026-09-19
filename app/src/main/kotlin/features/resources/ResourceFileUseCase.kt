// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package features.resources

import android.content.Context
import android.net.Uri
import app.CustomResourceFileState
import app.ResourceFileKind
import app.ResourceFilesStatus
import app.ResourceFileUpdateSource
import features.resources.runtime.AndroidResourceFileRepository
import features.resources.runtime.XrayCoreReleaseCheck
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import system.AndroidRootShellGateway
import system.RootShellGateway

class ResourceFileUseCase(
    context: Context,
    private val resourceFilePicker: suspend () -> Uri?,
    currentRunMode: () -> Int,
    rootShell: RootShellGateway = AndroidRootShellGateway(),
) {
    private val repository = AndroidResourceFileRepository(
        context = context.applicationContext,
        currentRunMode = currentRunMode,
        rootShell = rootShell,
    )

    suspend fun status(customResourceFiles: List<CustomResourceFileState> = emptyList()): ResourceFilesStatus {
        return repository.status(customResourceFiles)
    }

    suspend fun hasCustomXrayCore(): Boolean = repository.hasCustomXrayCore()

    suspend fun synchronizeBundledFilesAfterPackageUpdate(resourceFileSource: Int) {
        repository.synchronizeBundledFilesAfterPackageUpdate(resourceFileSource)
    }

    suspend fun update(
        source: ResourceFileUpdateSource,
        options: ResourceFileUpdateOptions = ResourceFileUpdateOptions(),
        customResourceFiles: List<CustomResourceFileState> = emptyList(),
    ): ResourceFilesStatus {
        return repository.update(source, options, customResourceFiles)
    }

    suspend fun update(
        kind: ResourceFileKind,
        source: ResourceFileUpdateSource,
        options: ResourceFileUpdateOptions = ResourceFileUpdateOptions(),
        customResourceFiles: List<CustomResourceFileState> = emptyList(),
    ): ResourceFilesStatus {
        return repository.update(kind, source, options, customResourceFiles)
    }

    suspend fun updateCustom(
        customFile: CustomResourceFileState,
        options: ResourceFileUpdateOptions = ResourceFileUpdateOptions(),
        customResourceFiles: List<CustomResourceFileState> = emptyList(),
    ): ResourceFilesStatus {
        return repository.updateCustom(customFile, options, customResourceFiles)
    }

    fun installedXrayCoreVersion(): String {
        return repository.installedXrayCoreVersion()
    }

    suspend fun refreshInstalledXrayCoreVersion(): String {
        return withContext(Dispatchers.IO) {
            repository.refreshInstalledXrayCoreVersion()
        }
    }

    suspend fun checkXrayCoreRelease(
        options: ResourceFileUpdateOptions = ResourceFileUpdateOptions(),
    ): XrayCoreReleaseCheck {
        return repository.checkXrayCoreRelease(options)
    }

    suspend fun updateXrayCore(
        version: String,
        downloadUrl: String,
        options: ResourceFileUpdateOptions = ResourceFileUpdateOptions(),
        customResourceFiles: List<CustomResourceFileState> = emptyList(),
    ): ResourceFilesStatus {
        return repository.updateXrayCore(version, downloadUrl, options, customResourceFiles)
    }

    suspend fun renameCustom(
        previousFile: CustomResourceFileState,
        customFile: CustomResourceFileState,
        customResourceFiles: List<CustomResourceFileState> = emptyList(),
    ): ResourceFilesStatus {
        return repository.renameCustom(previousFile, customFile, customResourceFiles)
    }

    suspend fun replace(
        kind: ResourceFileKind,
        customResourceFiles: List<CustomResourceFileState> = emptyList(),
    ): ResourceFilesStatus? {
        val uri = resourceFilePicker() ?: return null
        return repository.replace(kind, uri, customResourceFiles)
    }

    suspend fun replaceCustom(
        customFile: CustomResourceFileState,
        customResourceFiles: List<CustomResourceFileState> = emptyList(),
    ): ResourceFilesStatus? {
        val uri = resourceFilePicker() ?: return null
        return repository.replaceCustom(customFile, uri, customResourceFiles)
    }

    suspend fun restoreBundled(
        kind: ResourceFileKind,
        customResourceFiles: List<CustomResourceFileState> = emptyList(),
    ): ResourceFilesStatus {
        return repository.restoreBundled(kind, customResourceFiles)
    }

    suspend fun deleteCustom(
        customFile: CustomResourceFileState,
        customResourceFiles: List<CustomResourceFileState> = emptyList(),
    ): ResourceFilesStatus {
        return repository.deleteCustom(customFile, customResourceFiles)
    }
}

data class ResourceFileUpdateOptions(
    val useRunningProxy: Boolean = false,
    val fallbackProxyPort: Int? = null,
    val fallbackProxyUsername: String = "",
    val fallbackProxyPassword: String = "",
)
