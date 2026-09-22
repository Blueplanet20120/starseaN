// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package features.about

import android.content.Intent
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.FileProvider
import app.LocalAppServices
import app.LocalAppStateStore
import app.ProjectInfo
import app.R
import app.collectAppState
import features.resources.XrayCoreReleaseNotesView
import features.resources.runtime.AndroidResourceFileDownloadCancellation
import features.resources.runtime.AndroidResourceFileDownloadCancelledException
import features.resources.runtime.AndroidResourceFileDownloader
import java.io.File
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import top.yukonga.miuix.kmp.basic.Card
import top.yukonga.miuix.kmp.basic.InfiniteProgressIndicator
import top.yukonga.miuix.kmp.basic.SmallTitle
import top.yukonga.miuix.kmp.basic.Text
import top.yukonga.miuix.kmp.basic.TextButton
import top.yukonga.miuix.kmp.preference.ArrowPreference
import top.yukonga.miuix.kmp.theme.MiuixTheme
import top.yukonga.miuix.kmp.window.WindowDialog
import ui.text.formatTemplate

@Composable
internal fun AboutUpdateSection(
    modifier: Modifier = Modifier,
) {
    val services = LocalAppServices.current
    val appState by LocalAppStateStore.current.collectAppState()
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val client = remember { AppReleaseClient() }
    val downloader = remember { AndroidResourceFileDownloader() }
    val mainHandler = remember { Handler(Looper.getMainLooper()) }
    val installedVersion = displayAppVersion(ProjectInfo.VERSION_NAME)
    val alreadyLatestMessage = stringResource(R.string.about_already_latest)
        .formatTemplate("version" to installedVersion)
    val checkFailedMessage = stringResource(R.string.about_check_update_failed)
    val downloadFailedMessage = stringResource(R.string.about_update_download_failed)
    val allowInstallMessage = stringResource(R.string.about_update_allow_install)
    var checking by remember { mutableStateOf(false) }
    var availableRelease by remember { mutableStateOf<AppRelease?>(null) }
    var downloading by remember { mutableStateOf(false) }
    var downloadPercent by remember { mutableIntStateOf(0) }
    var readyApk by remember { mutableStateOf<File?>(null) }
    var downloadJob by remember { mutableStateOf<Job?>(null) }

    fun stopDownload() {
        downloadJob?.cancel()
        AndroidResourceFileDownloadCancellation.cancel()
        downloading = false
        readyApk = null
    }

    fun installReadyApk(file: File) {
        if (!context.packageManager.canRequestPackageInstalls()) {
            context.startActivity(unknownSourcesSettingsIntent(context.packageName))
            scope.launch { services.tipNotifier.show(allowInstallMessage) }
            return
        }
        val opened = runCatching { context.startActivity(apkInstallIntent(context, file)) }.isSuccess
        if (opened) {
            availableRelease = null
            readyApk = null
        } else {
            scope.launch { services.tipNotifier.show(downloadFailedMessage) }
        }
    }

    SmallTitle(text = stringResource(R.string.about_update))
    Card(
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp)
            .padding(bottom = 12.dp),
    ) {
        ArrowPreference(
            title = stringResource(R.string.about_check_update),
            summary = if (checking) {
                stringResource(R.string.about_checking_update)
            } else {
                stringResource(R.string.about_check_update_summary)
                    .formatTemplate("version" to installedVersion)
            },
            onClick = {
                if (!checking) {
                    checking = true
                    val proxy = appState.appReleaseProxy()
                    scope.launch {
                        val result = withContext(Dispatchers.IO) {
                            runCatching { client.check(installedVersion, proxy) }
                        }
                        checking = false
                        result.onSuccess { check ->
                            if (check.isNewer) {
                                availableRelease = check.latest
                            } else {
                                services.tipNotifier.show(alreadyLatestMessage)
                            }
                        }.onFailure {
                            services.tipNotifier.show(checkFailedMessage)
                        }
                    }
                }
            },
        )
    }

    AppUpdateDialog(
        release = availableRelease,
        installedVersion = installedVersion,
        downloading = downloading,
        downloadPercent = downloadPercent,
        readyToInstall = readyApk != null,
        onDismissRequest = {
            if (downloading) stopDownload()
            availableRelease = null
            readyApk = null
        },
        onUpdate = {
            val current = availableRelease ?: return@AppUpdateDialog
            val url = current.downloadUrl
            if (url.isBlank()) {
                scope.launch { services.tipNotifier.show(downloadFailedMessage) }
                return@AppUpdateDialog
            }
            val existing = readyApk
            if (existing != null && existing.isFile && existing.length() > 0L) {
                installReadyApk(existing)
                return@AppUpdateDialog
            }
            if (downloading) return@AppUpdateDialog
            val proxy = appState.appReleaseProxy()
            val file = updateApkFile(context, current.version)
            downloading = true
            downloadPercent = 0
            downloadJob = scope.launch {
                try {
                    withContext(Dispatchers.IO) {
                        AndroidResourceFileDownloadCancellation.begin()
                        var lastPercent = Int.MIN_VALUE
                        downloader.download(url, file, proxy) { downloaded, total ->
                            val percent = if (total > 0L) {
                                ((downloaded * 100) / total).toInt().coerceIn(0, 100)
                            } else {
                                -1
                            }
                            if (percent != lastPercent) {
                                lastPercent = percent
                                mainHandler.post { downloadPercent = percent }
                            }
                        }
                    }
                    downloading = false
                    readyApk = file
                    installReadyApk(file)
                } catch (error: CancellationException) {
                    file.delete()
                    downloading = false
                    readyApk = null
                    throw error
                } catch (_: AndroidResourceFileDownloadCancelledException) {
                    file.delete()
                    downloading = false
                    readyApk = null
                } catch (_: Throwable) {
                    file.delete()
                    downloading = false
                    readyApk = null
                    services.tipNotifier.show(downloadFailedMessage)
                }
            }
        },
    )
}

@Composable
private fun AppUpdateDialog(
    release: AppRelease?,
    installedVersion: String,
    downloading: Boolean,
    downloadPercent: Int,
    readyToInstall: Boolean,
    onDismissRequest: () -> Unit,
    onUpdate: () -> Unit,
) {
    WindowDialog(
        show = release != null,
        title = stringResource(R.string.about_update_title),
        onDismissRequest = onDismissRequest,
        content = {
            val currentRelease = release
            if (currentRelease != null) Column(modifier = Modifier.fillMaxWidth()) {
                Text(
                    text = currentRelease.title,
                    fontSize = 16.sp,
                    fontWeight = FontWeight.SemiBold,
                    color = MiuixTheme.colorScheme.onSurface,
                    modifier = Modifier.padding(bottom = 8.dp),
                )
                Text(
                    text = stringResource(R.string.about_update_current)
                        .formatTemplate("version" to installedVersion),
                    style = MiuixTheme.textStyles.body2,
                    color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                    modifier = Modifier.padding(bottom = 4.dp),
                )
                Text(
                    text = stringResource(R.string.about_update_latest)
                        .formatTemplate("version" to currentRelease.version),
                    style = MiuixTheme.textStyles.body2,
                    color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                    modifier = Modifier.padding(bottom = 12.dp),
                )
                if (currentRelease.notes.isNotBlank()) {
                    XrayCoreReleaseNotesView(
                        notes = currentRelease.notes,
                        modifier = Modifier.padding(bottom = 16.dp),
                    )
                }
                if (downloading) {
                    UpdateDownloadProgress(
                        percent = downloadPercent,
                        modifier = Modifier.padding(bottom = 16.dp),
                    )
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                ) {
                    TextButton(
                        text = stringResource(R.string.common_cancel),
                        onClick = onDismissRequest,
                        modifier = Modifier.weight(1f),
                    )
                    Spacer(Modifier.width(20.dp))
                    TextButton(
                        text = stringResource(
                            if (readyToInstall) R.string.about_update_install else R.string.common_update,
                        ),
                        onClick = onUpdate,
                        enabled = !downloading,
                        modifier = Modifier.weight(1f),
                    )
                }
            }
        },
    )
}

@Composable
private fun UpdateDownloadProgress(
    percent: Int,
    modifier: Modifier = Modifier,
) {
    val known = percent >= 0
    Column(modifier = modifier.fillMaxWidth()) {
        Text(
            text = if (known) {
                stringResource(R.string.about_update_downloading)
                    .formatTemplate("percent" to percent.coerceIn(0, 100))
            } else {
                stringResource(R.string.about_update_downloading_unknown)
            },
            style = MiuixTheme.textStyles.body2,
            color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
            modifier = Modifier.padding(bottom = 8.dp),
        )
        if (known) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(6.dp)
                    .clip(RoundedCornerShape(3.dp))
                    .background(MiuixTheme.colorScheme.onSurface.copy(alpha = 0.12f)),
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxHeight()
                        .fillMaxWidth((percent.coerceIn(0, 100) / 100f).coerceAtLeast(0.02f))
                        .background(MiuixTheme.colorScheme.primary),
                )
            }
        } else {
            InfiniteProgressIndicator(
                color = MiuixTheme.colorScheme.primary,
                size = 22.dp,
                strokeWidth = 2.dp,
            )
        }
    }
}

private fun updateApkFile(context: android.content.Context, version: String): File {
    val safe = version.filter { it.isLetterOrDigit() || it == '.' || it == '-' }.ifEmpty { "update" }
    val directory = File(context.cacheDir, "updates").apply { mkdirs() }
    return File(directory, "starseaN-$safe.apk")
}

private fun apkInstallIntent(context: android.content.Context, file: File): Intent {
    val uri = FileProvider.getUriForFile(context, "${context.packageName}.fileprovider", file)
    return Intent(Intent.ACTION_VIEW).apply {
        setDataAndType(uri, "application/vnd.android.package-archive")
        addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
}

private fun unknownSourcesSettingsIntent(packageName: String): Intent {
    return Intent(
        Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
        Uri.parse("package:$packageName"),
    ).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
}
