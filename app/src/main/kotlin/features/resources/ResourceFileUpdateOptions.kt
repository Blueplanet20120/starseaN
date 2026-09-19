// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package features.resources

import app.AppState
import engine.network.toPortOrNull

internal fun AppState.resourceFileUpdateOptions(): ResourceFileUpdateOptions {
    return ResourceFileUpdateOptions(
        useRunningProxy = enableResourceUpdateViaProxy && proxyRunning,
        fallbackProxyPort = localProxyPort.toPortOrNull(),
        fallbackProxyUsername = localProxyUsername,
        fallbackProxyPassword = localProxyPassword,
    )
}
