// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package features.about

import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.LocalAppServices
import app.LocalAppStateStore
import app.ProjectInfo
import app.R
import app.collectAppState
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import top.yukonga.miuix.kmp.basic.Card
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
    val installedVersion = displayAppVersion(ProjectInfo.VERSION_NAME)
    val alreadyLatestMessage = stringResource(R.string.about_already_latest)
        .formatTemplate("version" to installedVersion)
    val checkFailedMessage = stringResource(R.string.about_check_update_failed)
    var checking by remember { mutableStateOf(false) }
    var availableRelease by remember { mutableStateOf<AppRelease?>(null) }

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

    val release = availableRelease
    AppUpdateDialog(
        release = release,
        installedVersion = installedVersion,
        onDismissRequest = { availableRelease = null },
        onUpdate = {
            val current = availableRelease
            val url = current?.downloadUrl?.takeIf { it.isNotBlank() } ?: current?.pageUrl.orEmpty()
            availableRelease = null
            if (url.isBlank()) {
                scope.launch { services.tipNotifier.show(checkFailedMessage) }
                return@AppUpdateDialog
            }
            val opened = runCatching {
                context.startActivity(
                    Intent(Intent.ACTION_VIEW, Uri.parse(url)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
                )
            }.isSuccess
            if (!opened) {
                scope.launch { services.tipNotifier.show(checkFailedMessage) }
            }
        },
    )
}

@Composable
private fun AppUpdateDialog(
    release: AppRelease?,
    installedVersion: String,
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
                    Text(
                        text = currentRelease.notes,
                        style = MiuixTheme.textStyles.body2,
                        color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                        modifier = Modifier
                            .padding(bottom = 16.dp)
                            .heightIn(max = 180.dp)
                            .verticalScroll(rememberScrollState()),
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
                        text = stringResource(R.string.common_update),
                        onClick = onUpdate,
                        modifier = Modifier.weight(1f),
                    )
                }
            }
        },
    )
}
