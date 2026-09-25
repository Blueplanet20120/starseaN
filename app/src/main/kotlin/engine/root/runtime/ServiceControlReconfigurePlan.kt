// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package engine.root.runtime

import engine.root.daemon.control.StarseadPhase
import engine.root.publication.RootPublicationLaunchMode

internal data class ServiceControlReconfigurePlan(
    val shutdownRequired: Boolean,
    val launchMode: RootPublicationLaunchMode,
)

internal fun serviceControlReconfigurePlan(
    phase: StarseadPhase?,
    enabled: Boolean,
): ServiceControlReconfigurePlan =
    when (phase) {
        null -> ServiceControlReconfigurePlan(
            shutdownRequired = false,
            launchMode = if (enabled) {
                RootPublicationLaunchMode.Monitor
            } else {
                RootPublicationLaunchMode.None
            },
        )
        StarseadPhase.Running -> ServiceControlReconfigurePlan(
            shutdownRequired = true,
            launchMode = RootPublicationLaunchMode.Service,
        )
        StarseadPhase.Stopped, StarseadPhase.Paused -> ServiceControlReconfigurePlan(
            shutdownRequired = true,
            launchMode = if (enabled) {
                RootPublicationLaunchMode.Monitor
            } else {
                RootPublicationLaunchMode.None
            },
        )
        else -> throw IllegalArgumentException("starsead must be running or stopped to reconfigure")
    }
