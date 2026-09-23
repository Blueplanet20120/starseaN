// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package features.resources.runtime

import app.ProjectInfo
import features.resources.runtime.writeResourceAtomically as writeAtomically

import java.io.File
import java.io.IOException
import java.net.Authenticator
import java.net.HttpURLConnection
import java.net.InetSocketAddress
import java.net.PasswordAuthentication
import java.net.Proxy
import java.net.URI
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.nio.file.StandardOpenOption
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference

internal class ResourceFileHttpException(
    val code: Int,
) : IOException("HTTP $code")

internal class AndroidResourceFileDownloader {
    fun download(
        url: String,
        target: File,
        proxy: AndroidResourceFileDownloadProxy? = null,
        onProgress: (downloadedBytes: Long, totalBytes: Long) -> Unit = { _, _ -> },
    ) {
        if (proxy != null) {
            proxy.withAuthenticator {
                try {
                    downloadWithRetries(url, target, proxy, onProgress)
                    return
                } catch (_: IOException) {
                    AndroidResourceFileLogger.info("Proxy download failed, falling back to direct connection")
                }
            }
        }
        downloadWithRetries(url, target, null, onProgress)
    }

    fun downloadMultipart(
        url: String,
        target: File,
        proxy: AndroidResourceFileDownloadProxy? = null,
        workers: Int = MultipartWorkers,
        onProgress: (downloadedBytes: Long, totalBytes: Long) -> Unit = { _, _ -> },
    ) {
        val routes = buildList {
            if (proxy != null) add(proxy)
            add(null)
        }.distinct()
        var lastError: Throwable? = null
        for (route in routes) {
            try {
                runWithProxy(route) {
                    downloadMultipartOrSingle(url, target, route, workers, onProgress)
                }
                return
            } catch (error: Throwable) {
                if (error is AndroidResourceFileDownloadCancelledException ||
                    AndroidResourceFileDownloadCancellation.isCancelled()
                ) {
                    throw AndroidResourceFileDownloadCancelledException()
                }
                lastError = error
                if (route == null) throw error
                target.delete()
                AndroidResourceFileLogger.info("Proxy download failed, falling back to direct connection")
            }
        }
        throw lastError ?: error("Download failed")
    }

    private fun downloadMultipartOrSingle(
        url: String,
        target: File,
        proxy: AndroidResourceFileDownloadProxy?,
        workers: Int,
        onProgress: (downloadedBytes: Long, totalBytes: Long) -> Unit,
    ) {
        val remote = probeRemoteFile(url, proxy)
        val partCount = workers.coerceIn(1, MultipartWorkers)
        if (remote.ranged && remote.total >= MultipartMinBytes && partCount > 1) {
            try {
                AndroidResourceFileLogger.info(
                    "Multipart download parts=$partCount bytes=${remote.total} proxy=${proxy != null}",
                )
                downloadRanges(remote, target, proxy, partCount, onProgress)
                return
            } catch (error: Throwable) {
                if (error is AndroidResourceFileDownloadCancelledException ||
                    AndroidResourceFileDownloadCancellation.isCancelled()
                ) {
                    throw error
                }
                target.delete()
                AndroidResourceFileLogger.info("Multipart download failed, using one connection")
            }
        }
        downloadWithRetries(remote.url, target, proxy, onProgress)
    }

    private fun probeRemoteFile(
        url: String,
        proxy: AndroidResourceFileDownloadProxy?,
    ): RemoteFile {
        val finalUrl = resolveRedirectChain(url, proxy).last()
        val connection = URI.create(finalUrl).toUrlConnection(proxy)
        try {
            connection.setRequestProperty("Range", "bytes=0-0")
            AndroidResourceFileDownloadCancellation.track(connection)
            val code = connection.responseCode
            AndroidResourceFileDownloadCancellation.throwIfCancelled()
            if (code == HttpURLConnection.HTTP_PARTIAL) {
                val total = contentRangeTotal(connection.getHeaderField("Content-Range"))
                if (total > 0L) return RemoteFile(finalUrl, total, ranged = true)
            }
            return RemoteFile(finalUrl, connection.contentLengthLong, ranged = false)
        } finally {
            AndroidResourceFileDownloadCancellation.untrack(connection)
            connection.disconnect()
        }
    }

    private fun downloadRanges(
        remote: RemoteFile,
        target: File,
        proxy: AndroidResourceFileDownloadProxy?,
        workers: Int,
        onProgress: (downloadedBytes: Long, totalBytes: Long) -> Unit,
    ) {
        target.parentFile?.mkdirs()
        FileChannel.open(
            target.toPath(),
            StandardOpenOption.CREATE,
            StandardOpenOption.WRITE,
            StandardOpenOption.TRUNCATE_EXISTING,
        ).use { channel ->
            channel.truncate(remote.total)
        }
        val chunk = remote.total / workers
        val downloaded = AtomicLong(0L)
        val firstError = AtomicReference<Throwable?>(null)
        val pool = Executors.newFixedThreadPool(workers) { runnable ->
            Thread(runnable, "app-update-download").apply { isDaemon = true }
        }
        try {
            val tasks = (0 until workers).map { index ->
                val start = index * chunk
                val end = if (index == workers - 1) remote.total - 1 else (index + 1) * chunk - 1
                pool.submit {
                    try {
                        downloadRange(remote.url, target, proxy, start, end) { count ->
                            onProgress(downloaded.addAndGet(count), remote.total)
                        }
                    } catch (error: Throwable) {
                        if (firstError.compareAndSet(null, error) &&
                            !AndroidResourceFileDownloadCancellation.isCancelled()
                        ) {
                            AndroidResourceFileDownloadCancellation.disconnectTracked()
                        }
                    }
                }
            }
            tasks.forEach { task -> task.get() }
            val error = firstError.get()
            if (error != null) throw error
            if (downloaded.get() != remote.total) {
                error("Incomplete download ${downloaded.get()}/${remote.total}")
            }
        } finally {
            pool.shutdownNow()
        }
    }

    private fun downloadRange(
        url: String,
        target: File,
        proxy: AndroidResourceFileDownloadProxy?,
        start: Long,
        end: Long,
        onBytes: (Long) -> Unit,
    ) {
        val connection = URI.create(url).toUrlConnection(proxy)
        try {
            connection.setRequestProperty("Range", "bytes=$start-$end")
            AndroidResourceFileDownloadCancellation.track(connection)
            val code = connection.responseCode
            AndroidResourceFileDownloadCancellation.throwIfCancelled()
            if (code != HttpURLConnection.HTTP_PARTIAL) {
                throw IOException("Range request was not accepted")
            }
            connection.inputStream.use { input ->
                FileChannel.open(target.toPath(), StandardOpenOption.WRITE).use { channel ->
                    val buffer = ByteArray(MultipartBufferSize)
                    var position = start
                    while (true) {
                        AndroidResourceFileDownloadCancellation.throwIfCancelled()
                        val read = input.read(buffer)
                        if (read < 0) break
                        val data = ByteBuffer.wrap(buffer, 0, read)
                        while (data.hasRemaining()) {
                            val written = channel.write(data, position)
                            if (written <= 0) throw IOException("Write stalled")
                            position += written
                        }
                        onBytes(read.toLong())
                    }
                    if (position != end + 1) {
                        throw IOException("Short range $start-$end")
                    }
                }
            }
        } finally {
            AndroidResourceFileDownloadCancellation.untrack(connection)
            connection.disconnect()
        }
    }

    private fun runWithProxy(
        proxy: AndroidResourceFileDownloadProxy?,
        block: () -> Unit,
    ) {
        if (proxy == null) {
            block()
        } else {
            proxy.withAuthenticator(block)
        }
    }

    private fun downloadWithRetries(
        url: String,
        target: File,
        proxy: AndroidResourceFileDownloadProxy?,
        onProgress: (downloadedBytes: Long, totalBytes: Long) -> Unit,
    ) {
        var lastError: Throwable? = null
        repeat(MaxRetries) { attempt ->
            try {
                downloadWithRedirects(url, target, proxy, onProgress)
                return
            } catch (error: Throwable) {
                if (AndroidResourceFileDownloadCancellation.isCancelled()) {
                    throw AndroidResourceFileDownloadCancelledException()
                }
                if (error.isRetryableDownloadFailure() && attempt < MaxRetries - 1) {
                    lastError = error
                    Thread.sleep(RetryBackoffMs * (1L shl attempt))
                } else {
                    throw error
                }
            }
        }
        throw lastError ?: error("Download failed")
    }

    private fun downloadWithRedirects(
        url: String,
        target: File,
        proxy: AndroidResourceFileDownloadProxy?,
        onProgress: (downloadedBytes: Long, totalBytes: Long) -> Unit,
    ) {
        var currentUrl = url
        repeat(MaxRedirects) {
            val connection = URI.create(currentUrl).toUrlConnection(proxy)
            try {
                AndroidResourceFileDownloadCancellation.track(connection)
                AndroidResourceFileDownloadCancellation.throwIfCancelled()
                val code = connection.responseCode
                AndroidResourceFileDownloadCancellation.throwIfCancelled()
                if (code in 300..399) {
                    val location = connection.getHeaderField("Location")
                        ?: error("Redirect location missing")
                    currentUrl = URI(currentUrl).resolve(location).toString()
                    return@repeat
                }
                if (code !in 200..299) {
                    throw ResourceFileHttpException(code)
                }
                val totalBytes = connection.contentLengthLong
                connection.inputStream.use { input ->
                    writeAtomically(target) { output ->
                        input.copyToWithProgress(output, totalBytes, onProgress)
                    }
                }
                return
            } finally {
                AndroidResourceFileDownloadCancellation.untrack(connection)
                connection.disconnect()
            }
        }
        error("Too many redirects")
    }

    fun fetchText(
        url: String,
        proxy: AndroidResourceFileDownloadProxy? = null,
        extraHeaders: Map<String, String> = emptyMap(),
    ): String {
        if (proxy != null) {
            proxy.withAuthenticator {
                try {
                    return fetchTextWithRetries(url, proxy, extraHeaders)
                } catch (_: IOException) {
                    AndroidResourceFileLogger.info("Proxy download failed, falling back to direct connection")
                }
            }
        }
        return fetchTextWithRetries(url, null, extraHeaders)
    }

    private fun fetchTextWithRetries(
        url: String,
        proxy: AndroidResourceFileDownloadProxy?,
        extraHeaders: Map<String, String>,
    ): String {
        var lastError: Throwable? = null
        repeat(MaxRetries) { attempt ->
            try {
                return fetchTextWithRedirects(url, proxy, extraHeaders)
            } catch (error: Throwable) {
                if (AndroidResourceFileDownloadCancellation.isCancelled()) {
                    throw AndroidResourceFileDownloadCancelledException()
                }
                if (error.isRetryableDownloadFailure() && attempt < MaxRetries - 1) {
                    lastError = error
                    Thread.sleep(RetryBackoffMs * (1L shl attempt))
                } else {
                    throw error
                }
            }
        }
        throw lastError ?: error("Download failed")
    }

    private fun fetchTextWithRedirects(
        url: String,
        proxy: AndroidResourceFileDownloadProxy?,
        extraHeaders: Map<String, String>,
    ): String {
        var currentUrl = url
        repeat(MaxRedirects) {
            val connection = URI.create(currentUrl).toUrlConnection(proxy, extraHeaders)
            try {
                AndroidResourceFileDownloadCancellation.track(connection)
                AndroidResourceFileDownloadCancellation.throwIfCancelled()
                val code = connection.responseCode
                AndroidResourceFileDownloadCancellation.throwIfCancelled()
                if (code in 300..399) {
                    val location = connection.getHeaderField("Location")
                        ?: error("Redirect location missing")
                    currentUrl = URI(currentUrl).resolve(location).toString()
                    return@repeat
                }
                if (code !in 200..299) {
                    throw ResourceFileHttpException(code)
                }
                return connection.inputStream.bufferedReader(Charsets.UTF_8).use { it.readText() }
            } finally {
                AndroidResourceFileDownloadCancellation.untrack(connection)
                connection.disconnect()
            }
        }
        error("Too many redirects")
    }

    fun resolveRedirectChain(
        url: String,
        proxy: AndroidResourceFileDownloadProxy? = null,
    ): List<String> {
        if (proxy != null) {
            proxy.withAuthenticator {
                try {
                    return resolveRedirectChainOnce(url, proxy)
                } catch (_: IOException) {
                    AndroidResourceFileLogger.info("Proxy download failed, falling back to direct connection")
                }
            }
        }
        return resolveRedirectChainOnce(url, null)
    }

    private fun resolveRedirectChainOnce(
        url: String,
        proxy: AndroidResourceFileDownloadProxy?,
    ): List<String> {
        val hops = mutableListOf(url)
        var currentUrl = url
        repeat(MaxRedirects) {
            val connection = URI.create(currentUrl).toUrlConnection(proxy)
            try {
                connection.requestMethod = "HEAD"
                AndroidResourceFileDownloadCancellation.track(connection)
                AndroidResourceFileDownloadCancellation.throwIfCancelled()
                val code = connection.responseCode
                AndroidResourceFileDownloadCancellation.throwIfCancelled()
                if (code == HttpURLConnection.HTTP_BAD_METHOD || code == 501) {
                    return resolveRedirectChainWithGet(url, proxy)
                }
                if (code in 300..399) {
                    val location = connection.getHeaderField("Location")
                        ?: error("Redirect location missing")
                    currentUrl = URI(currentUrl).resolve(location).toString()
                    hops += currentUrl
                    return@repeat
                }
                if (code !in 200..299) {
                    throw ResourceFileHttpException(code)
                }
                return hops
            } finally {
                AndroidResourceFileDownloadCancellation.untrack(connection)
                connection.disconnect()
            }
        }
        error("Too many redirects")
    }

    private fun resolveRedirectChainWithGet(
        url: String,
        proxy: AndroidResourceFileDownloadProxy?,
    ): List<String> {
        val hops = mutableListOf(url)
        var currentUrl = url
        repeat(MaxRedirects) {
            val connection = URI.create(currentUrl).toUrlConnection(proxy)
            try {
                AndroidResourceFileDownloadCancellation.track(connection)
                AndroidResourceFileDownloadCancellation.throwIfCancelled()
                val code = connection.responseCode
                AndroidResourceFileDownloadCancellation.throwIfCancelled()
                if (code in 300..399) {
                    val location = connection.getHeaderField("Location")
                        ?: error("Redirect location missing")
                    currentUrl = URI(currentUrl).resolve(location).toString()
                    hops += currentUrl
                    return@repeat
                }
                if (code !in 200..299) {
                    throw ResourceFileHttpException(code)
                }
                return hops
            } finally {
                AndroidResourceFileDownloadCancellation.untrack(connection)
                connection.disconnect()
            }
        }
        error("Too many redirects")
    }
}

internal data class AndroidResourceFileDownloadProxy(
    val host: String,
    val port: Int,
    val username: String,
    val password: String,
)

private fun URI.toUrlConnection(proxy: AndroidResourceFileDownloadProxy?): HttpURLConnection {
    val url = toURL()
    val connection = if (proxy == null) {
        url.openConnection()
    } else {
        url.openConnection(proxy.toJavaProxy())
    }
    return (connection as HttpURLConnection).apply {
        connectTimeout = 15_000
        readTimeout = 60_000
        instanceFollowRedirects = false
        requestMethod = "GET"
        setRequestProperty(
            "User-Agent",
            ResourceFileDefaultUserAgent,
        )
    }
}

internal fun URI.toUrlConnection(
    proxy: AndroidResourceFileDownloadProxy?,
    extraHeaders: Map<String, String>,
): HttpURLConnection {
    return toUrlConnection(proxy).apply {
        extraHeaders.forEach { (key, value) ->
            setRequestProperty(key, value)
        }
    }
}

private fun AndroidResourceFileDownloadProxy.toJavaProxy(): Proxy {
    return Proxy(Proxy.Type.SOCKS, InetSocketAddress(host, port))
}

private inline fun <T> AndroidResourceFileDownloadProxy?.withAuthenticator(block: () -> T): T {
    if (this == null || username.isBlank()) return block()
    synchronized(ProxyAuthenticatorLock) {
        Authenticator.setDefault(toAuthenticator())
        return try {
            block()
        } finally {
            Authenticator.setDefault(null)
        }
    }
}

private fun AndroidResourceFileDownloadProxy.toAuthenticator(): Authenticator {
    return object : Authenticator() {
        override fun getPasswordAuthentication(): PasswordAuthentication? {
            if (requestingHost != host || requestingPort != port) return null
            return PasswordAuthentication(username, password.toCharArray())
        }
    }
}

private val ProxyAuthenticatorLock = Any()

private const val MaxRedirects = 5
private const val MaxRetries = 3
private const val RetryBackoffMs = 1000L
private const val MultipartWorkers = 4
private const val MultipartMinBytes = 4L * 1024L * 1024L
private const val MultipartBufferSize = 64 * 1024

private data class RemoteFile(
    val url: String,
    val total: Long,
    val ranged: Boolean,
)

private fun contentRangeTotal(header: String?): Long {
    val total = header?.substringAfter('/', "")?.trim().orEmpty()
    return total.toLongOrNull() ?: -1L
}
private const val ResourceFileDefaultUserAgent =
    "${ProjectInfo.PROJECT_NAME}/${ProjectInfo.VERSION_NAME} (+https://github.com/Blueplanet20120/starseaN)"

private fun Throwable.isRetryableDownloadFailure(): Boolean {
    if (this is ResourceFileHttpException) {
        return code == 403 || code == 408 || code == 425 || code == 429 || code in 500..599
    }
    return this is IOException
}

internal fun overallProgress(
    fileIndex: Int,
    fileCount: Int,
    downloadedBytes: Long,
    totalBytes: Long,
): Int? {
    if (totalBytes <= 0L || fileCount <= 0) return null
    val completedFiles = fileIndex.coerceAtLeast(0).toDouble()
    val currentFileProgress = (downloadedBytes.toDouble() / totalBytes.toDouble()).coerceIn(0.0, 1.0)
    return (((completedFiles + currentFileProgress) / fileCount.toDouble()) * 100).toInt().coerceIn(0, 100)
}

private fun java.io.InputStream.copyToWithProgress(
    output: java.io.OutputStream,
    totalBytes: Long,
    onProgress: (downloadedBytes: Long, totalBytes: Long) -> Unit,
) {
    val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
    var downloadedBytes = 0L
    onProgress(downloadedBytes, totalBytes)
    while (true) {
        AndroidResourceFileDownloadCancellation.throwIfCancelled()
        val bytesRead = read(buffer)
        if (bytesRead < 0) break
        AndroidResourceFileDownloadCancellation.throwIfCancelled()
        output.write(buffer, 0, bytesRead)
        downloadedBytes += bytesRead
        onProgress(downloadedBytes, totalBytes)
    }
}
