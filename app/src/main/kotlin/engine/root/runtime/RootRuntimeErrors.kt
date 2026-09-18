// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package engine.root.runtime

import engine.root.daemon.control.StarseadFailureCode
import engine.root.daemon.control.StarseadPhase
import engine.root.daemon.control.StarseadSnapshot

internal class RootRuntimeConflictException(
    val snapshot: StarseadSnapshot,
) : IllegalStateException("ROOT runtime is owned by ${snapshot.owner.wireValue}")

internal class RootRuntimeBusyException(
    val snapshot: StarseadSnapshot,
) : IllegalStateException("starseaN ROOT runtime is ${snapshot.phase.wireValue} in ${snapshot.mode.wireValue} mode")

internal class RootProtocolException(
    operation: String,
    cause: Throwable,
) : IllegalStateException("Invalid starsead response for $operation", cause)

internal class RootPublicationException(
    stage: String,
    cause: Throwable,
) : IllegalStateException("ROOT publication failed during $stage", cause)

internal class RootStartFailedException(
    val code: StarseadFailureCode?,
    val phase: StarseadPhase?,
    cause: Throwable,
) : IllegalStateException("ROOT supervisor failed to start", cause)
