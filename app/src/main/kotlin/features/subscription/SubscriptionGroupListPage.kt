// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

@file:OptIn(ExperimentalScrollBarApi::class)

package features.subscription

import app.AppServices
import app.AppState
import app.LocalAppServices
import app.LocalAppStateStore
import app.LocalIsWideScreen
import app.LocalNavigator
import app.LocalUpdateAppState
import app.SubscriptionGroupState
import app.collectAppState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import app.R
import ui.components.BackNavigationIcon
import ui.components.DeleteConfirmationDialog
import ui.components.NavigationIcon
import androidx.compose.ui.res.stringResource
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.Scaffold
import top.yukonga.miuix.kmp.basic.SmallTitle
import top.yukonga.miuix.kmp.basic.VerticalScrollBar
import top.yukonga.miuix.kmp.basic.rememberScrollBarAdapter
import top.yukonga.miuix.kmp.icon.MiuixIcons
import top.yukonga.miuix.kmp.icon.extended.Add
import ui.layout.AdaptiveTopAppBar
import ui.text.formatTemplate
import ui.layout.pageContentPaddingWithCutout
import ui.layout.pageListPadding
import ui.layout.pageScrollModifiers
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import data.AndroidAppStateStore
import features.proxy.server.usecase.withUpdatedSubscriptionServers
import features.subscription.usecase.subscriptionUpdateMessage
import features.subscription.usecase.toSubscriptionFetchOptions
import features.subscription.usecase.updateSubscriptions
import kotlinx.coroutines.launch
import top.yukonga.miuix.kmp.interfaces.ExperimentalScrollBarApi

@Composable
fun SubscriptionGroupListPage(
    padding: PaddingValues,
) {
    val isWideScreen = LocalIsWideScreen.current
    val navigator = LocalNavigator.current
    val stateStore = LocalAppStateStore.current
    val services = LocalAppServices.current
    val appState by stateStore.collectAppState()
    val updateAppState = LocalUpdateAppState.current
    val topAppBarScrollBehavior = MiuixScrollBehavior()
    val scope = rememberCoroutineScope()
    val groups = appState.subscriptionGroups
    val subscriptionUpdateResultTemplate = stringResource(R.string.proxy_server_list_subscription_update_result)
    val subscriptionUpdateResultWithFailedTemplate =
        stringResource(R.string.proxy_server_list_subscription_update_result_with_failed)
    val invalidSubscriptionUrlMessage = stringResource(R.string.subscription_invalid_url)
    var editingGroupId by remember { mutableStateOf<Int?>(null) }
    var showGroupEditor by remember { mutableStateOf(false) }
    var pendingGroupDeletion by remember { mutableStateOf<SubscriptionGroupState?>(null) }
    val editingGroup = editingGroupId?.let { id -> groups.firstOrNull { it.id == id } }

    fun closeGroupEditor() {
        showGroupEditor = false
    }

    fun clearGroupEditor() {
        editingGroupId = null
    }

    fun saveGroup(group: SubscriptionGroupState, isNew: Boolean) {
        updateAppState { state ->
            if (isNew) {
                val savedGroup = group.copy(id = state.nextSubscriptionGroupId)
                state.copy(
                    subscriptionGroups = state.subscriptionGroups + savedGroup,
                    nextSubscriptionGroupId = state.nextSubscriptionGroupId + 1,
                )
            } else {
                state.copy(
                    subscriptionGroups = state.subscriptionGroups.map {
                        if (it.id == group.id) group else it
                    },
                )
            }
        }
    }

    fun deleteGroup(group: SubscriptionGroupState) {
        if (group.builtIn) return
        updateAppState { state ->
            val nextServers = state.proxyServers.filterNot { it.groupId == group.id }
            state.copy(
                subscriptionGroups = state.subscriptionGroups.filterNot { it.id == group.id },
                proxyServers = nextServers,
                selectedProxyServerId = if (nextServers.any { it.id == state.selectedProxyServerId }) {
                    state.selectedProxyServerId
                } else {
                    nextServers.firstOrNull()?.id ?: 0
                },
            )
        }
    }

    fun requestGroupDeletion(group: SubscriptionGroupState) {
        if (appState.enableDeletionConfirmation) {
            pendingGroupDeletion = group
        } else {
            deleteGroup(group)
        }
    }

    Scaffold(
        topBar = {
            AdaptiveTopAppBar(
                title = stringResource(R.string.subscription_group_list_title),
                subtitle = stringResource(R.string.subscription_group_list_count).formatTemplate("count" to groups.size),
                isWideScreen = isWideScreen,
                scrollBehavior = topAppBarScrollBehavior,
                navigationIcon = {
                    BackNavigationIcon(
                        onClick = { navigator.pop() },
                    )
                },
                actions = {
                    NavigationIcon(
                        onClick = {
                            editingGroupId = null
                            showGroupEditor = true
                        },
                        imageVector = MiuixIcons.Add,
                    )
                },
            )
        },
    ) { innerPadding ->
        val lazyListState = rememberLazyListState()
        val contentPadding = pageContentPaddingWithCutout(
            innerPadding = innerPadding,
            outerPadding = padding,
            isWideScreen = isWideScreen,
        )
        val listPadding = pageListPadding(contentPadding)

        Box {
            LazyColumn(
                state = lazyListState,
                modifier = Modifier.pageScrollModifiers(
                    topAppBarScrollBehavior,
                ),
                contentPadding = listPadding,
            ) {
                item(key = "subscription_title") {
                    SmallTitle(text = stringResource(R.string.subscription_group_list))
                }
                items(
                    items = groups,
                    key = { it.id },
                ) { group ->
                    SubscriptionGroupCard(
                        group = group,
                        onToggle = { enabled ->
                            updateAppState { state ->
                                state.copy(
                                    subscriptionGroups = state.subscriptionGroups.map {
                                        if (it.id == group.id) it.copy(enabled = enabled) else it
                                    },
                                )
                            }
                        },
                        onUpdate = if (group.url.isNotBlank()) {
                            {
                                updateSubscriptionGroup(
                                    group = group,
                                    stateStore = stateStore,
                                    services = services,
                                    updateAppState = updateAppState,
                                    successTemplate = subscriptionUpdateResultTemplate,
                                    failedTemplate = subscriptionUpdateResultWithFailedTemplate,
                                )
                            }
                        } else {
                            null
                        },
                        onEdit = {
                            editingGroupId = group.id
                            showGroupEditor = true
                        },
                        onDelete = { requestGroupDeletion(group) },
                    )
                }
            }
            VerticalScrollBar(
                adapter = rememberScrollBarAdapter(lazyListState),
                modifier = Modifier.align(Alignment.CenterEnd).fillMaxHeight(),
                trackPadding = contentPadding,
            )
        }
        SubscriptionGroupEditorDialog(
            show = showGroupEditor,
            group = editingGroup,
            nextGroupId = appState.nextSubscriptionGroupId,
            onDismissRequest = ::closeGroupEditor,
            onDismissFinished = ::clearGroupEditor,
            onSave = ::saveGroup,
            onInvalidUrl = {
                scope.launch { services.tipNotifier.show(invalidSubscriptionUrlMessage) }
            },
        )
        pendingGroupDeletion?.let { group ->
            DeleteConfirmationDialog(
                show = true,
                title = stringResource(R.string.deletion_confirmation_delete_subscription_group),
                onDismissRequest = { pendingGroupDeletion = null },
                onConfirm = {
                    pendingGroupDeletion = null
                    deleteGroup(group)
                },
            )
        }
    }
}

private fun updateSubscriptionGroup(
    group: SubscriptionGroupState,
    stateStore: AndroidAppStateStore,
    services: AppServices,
    updateAppState: ((AppState) -> AppState) -> Unit,
    successTemplate: String,
    failedTemplate: String,
) {
    if (group.url.isBlank()) return
    services.appScope.launch {
        val result = updateSubscriptions(
            groups = listOf(group),
            subscriptionFetcher = services.subscriptionFetcher,
            fetchOptions = { updateGroup -> stateStore.state.value.toSubscriptionFetchOptions(updateGroup) },
        )
        if (result.updates.isNotEmpty()) {
            updateAppState { state ->
                state.withUpdatedSubscriptionServers(
                    updates = result.updates,
                    updatedAtMillis = result.updatedAtMillis,
                )
            }
        }
        services.tipNotifier.show(
            subscriptionUpdateMessage(
                result = result,
                successTemplate = successTemplate,
                failedTemplate = failedTemplate,
            ),
        )
    }
}
