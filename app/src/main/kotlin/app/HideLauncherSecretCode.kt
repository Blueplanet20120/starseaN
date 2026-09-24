// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package app

internal const val DefaultHideLauncherSecretCode = "8866"
internal const val HideLauncherSecretCodeMinLength = 3
internal const val HideLauncherSecretCodeMaxLength = 6

internal fun normalizeHideLauncherSecretCode(value: String?): String {
    val code = value?.trim().orEmpty()
    val valid = code.length in HideLauncherSecretCodeMinLength..HideLauncherSecretCodeMaxLength &&
        code.all(Char::isDigit)
    return if (valid) code else DefaultHideLauncherSecretCode
}

internal fun sanitizeHideLauncherSecretCodeInput(value: String): String {
    return value.filter(Char::isDigit).take(HideLauncherSecretCodeMaxLength)
}
