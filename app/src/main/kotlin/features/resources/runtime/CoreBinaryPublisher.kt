// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package features.resources.runtime

import android.system.ErrnoException
import android.system.Os
import android.system.OsConstants
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.util.concurrent.ConcurrentHashMap

private val coreBinaryPublicationLocks = ConcurrentHashMap<String, Any>()

internal fun publishCoreBinaryCandidate(
    candidate: File,
    target: File,
    replaceExisting: Boolean,
): Boolean {
    val absoluteTarget = target.absoluteFile
    val publicationLock = coreBinaryPublicationLocks.computeIfAbsent(absoluteTarget.path) { Any() }
    return synchronized(publicationLock) {
        val parent = absoluteTarget.parentFile
            ?: error("Core binary target has no parent directory: ${absoluteTarget.path}")
        if (!parent.isDirectory && !parent.mkdirs()) {
            error("Failed to create core binary directory: ${parent.path}")
        }
        if (!replaceExisting && absoluteTarget.exists()) {
            return@synchronized false
        }

        val temporary = File.createTempFile(".${absoluteTarget.name}.", ".tmp", parent)
        try {
            candidate.inputStream().use { input ->
                FileOutputStream(temporary).use { output ->
                    input.copyTo(output)
                    output.flush()
                    output.fd.sync()
                }
            }
            temporary.applyCoreBinaryPermissions()
            if (absoluteTarget.exists() && !absoluteTarget.delete()) {
                error("Failed to replace core binary: ${absoluteTarget.path}")
            }
            if (!temporary.renameTo(absoluteTarget)) {
                error("Failed to publish core binary: ${absoluteTarget.path}")
            }
            absoluteTarget.applyCoreBinaryPermissions()
            true
        } finally {
            temporary.delete()
        }
    }
}

internal fun File.applyCoreBinaryPermissions() {
    try {
        Os.chmod(absolutePath, CoreBinaryPosixMode)
        return
    } catch (_: ErrnoException) {
    }
    if (!setReadable(true, false) || !setExecutable(true, false)) {
        throw IOException("Failed to make core binary readable and executable: $path")
    }
}

private val CoreBinaryPosixMode =
    OsConstants.S_IRUSR or OsConstants.S_IWUSR or OsConstants.S_IXUSR or
        OsConstants.S_IRGRP or OsConstants.S_IXGRP or
        OsConstants.S_IROTH or OsConstants.S_IXOTH
