// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package features.lock

import android.os.SystemClock
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.activity.compose.LocalActivity
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.fragment.app.FragmentActivity
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.ProcessLifecycleOwner
import app.LocalAppStateStore
import app.ProjectInfo
import app.R
import app.collectAppState
import app.modes.appLockTimeoutMillis
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import top.yukonga.miuix.kmp.basic.Text
import top.yukonga.miuix.kmp.basic.TextButton
import top.yukonga.miuix.kmp.theme.MiuixTheme

@Composable
internal fun AppLockHost(
    content: @Composable () -> Unit,
) {
    val appState by LocalAppStateStore.current.collectAppState()
    val activity = LocalActivity.current as? FragmentActivity
    var unlocked by rememberSaveable { mutableStateOf(false) }
    var promptAttempt by remember { mutableStateOf(0) }

    LaunchedEffect(appState.enableAppLock) {
        if (!appState.enableAppLock) {
            unlocked = true
        }
    }

    DisposableEffect(appState.enableAppLock, appState.appLockTimeout) {
        if (!appState.enableAppLock) {
            return@DisposableEffect onDispose { }
        }
        var receivedStop = false
        var hiddenAtElapsed = 0L
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_STOP -> {
                    receivedStop = true
                    hiddenAtElapsed = SystemClock.elapsedRealtime()
                }
                Lifecycle.Event.ON_START -> {
                    if (!receivedStop) {
                        return@LifecycleEventObserver
                    }
                    receivedStop = false
                    if (SystemClock.elapsedRealtime() - hiddenAtElapsed >=
                        appLockTimeoutMillis(appState.appLockTimeout)
                    ) {
                        val alreadyLocked = !unlocked
                        unlocked = false
                        if (alreadyLocked) {
                            promptAttempt += 1
                        }
                    }
                }
                else -> Unit
            }
        }
        val lifecycle = ProcessLifecycleOwner.get().lifecycle
        lifecycle.addObserver(observer)
        onDispose { lifecycle.removeObserver(observer) }
    }

    Box(modifier = Modifier.fillMaxSize()) {
        content()
        if (appState.enableAppLock && !unlocked) {
            AppLockOverlay(
                onUnlockRequest = { promptAttempt += 1 },
            )
            val promptTitle = stringResource(R.string.app_lock_prompt_title)
            val promptSubtitle = stringResource(R.string.app_lock_prompt_subtitle)
            LaunchedEffect(unlocked, promptAttempt, activity) {
                val host = activity ?: return@LaunchedEffect
                val success = withContext(Dispatchers.Main.immediate) {
                    host.promptAppLock(
                        title = promptTitle,
                        subtitle = promptSubtitle,
                    )
                }
                if (success) {
                    unlocked = true
                }
            }
        }
    }
}

@Composable
private fun AppLockOverlay(
    onUnlockRequest: () -> Unit,
) {
    BackHandler(enabled = true) { }
    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MiuixTheme.colorScheme.background)
            .clickable(
                indication = null,
                interactionSource = remember { MutableInteractionSource() },
                onClick = {},
            ),
        contentAlignment = Alignment.Center,
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(20.dp),
            modifier = Modifier.padding(horizontal = 32.dp),
        ) {
            Text(
                text = ProjectInfo.PROJECT_NAME,
                style = MiuixTheme.textStyles.title2,
                color = MiuixTheme.colorScheme.onBackground,
            )
            Text(
                text = stringResource(R.string.app_lock_overlay_summary),
                style = MiuixTheme.textStyles.body2,
                color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
            )
            TextButton(
                text = stringResource(R.string.app_lock_unlock),
                onClick = onUnlockRequest,
            )
        }
    }
}
