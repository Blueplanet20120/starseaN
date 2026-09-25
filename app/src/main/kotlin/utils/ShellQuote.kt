// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package utils

internal fun String.shellQuote(): String {
    return "'${replace("'", "'\"'\"'")}'"
}
