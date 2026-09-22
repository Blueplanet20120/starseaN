// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package features.resources.runtime

import java.net.HttpURLConnection
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicBoolean

internal object AndroidResourceFileDownloadCancellation {
    private val cancelled = AtomicBoolean(false)
    private val connections = ConcurrentHashMap.newKeySet<HttpURLConnection>()

    fun begin() {
        cancelled.set(false)
        connections.clear()
    }

    fun cancel() {
        cancelled.set(true)
        disconnectTracked()
    }

    fun disconnectTracked() {
        connections.forEach { connection ->
            runCatching { connection.disconnect() }
        }
    }

    fun track(connection: HttpURLConnection) {
        connections.add(connection)
        if (cancelled.get()) {
            connection.disconnect()
            throw AndroidResourceFileDownloadCancelledException()
        }
    }

    fun untrack(connection: HttpURLConnection) {
        connections.remove(connection)
    }

    fun isCancelled(): Boolean {
        return cancelled.get()
    }

    fun throwIfCancelled() {
        if (cancelled.get()) {
            throw AndroidResourceFileDownloadCancelledException()
        }
    }
}

internal class AndroidResourceFileDownloadCancelledException(
    message: String = "Resource file update cancelled",
) : RuntimeException(message)
