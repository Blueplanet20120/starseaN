// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package features.lock

import android.app.KeyguardManager
import android.content.Context
import android.content.ContextWrapper
import android.os.Build
import androidx.biometric.BiometricManager
import androidx.biometric.BiometricManager.Authenticators
import androidx.biometric.BiometricPrompt
import androidx.core.content.ContextCompat
import androidx.fragment.app.FragmentActivity
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlin.coroutines.resume

internal fun Context.canUseAppLock(): Boolean {
    return appLockAuthenticatorsOrNull() != null
}

internal fun Context.findFragmentActivity(): FragmentActivity? {
    var current: Context? = this
    while (current is ContextWrapper) {
        if (current is FragmentActivity) {
            return current
        }
        current = current.baseContext
    }
    return current as? FragmentActivity
}

internal suspend fun FragmentActivity.promptAppLock(
    title: String,
    subtitle: String,
): Boolean {
    if (isFinishing || isDestroyed) {
        return false
    }
    val authenticators = appLockAuthenticatorsOrNull() ?: return false
    return suspendCancellableCoroutine { continuation ->
        val prompt = BiometricPrompt(
            this,
            ContextCompat.getMainExecutor(this),
            object : BiometricPrompt.AuthenticationCallback() {
                override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult) {
                    if (continuation.isActive) {
                        continuation.resume(true)
                    }
                }

                override fun onAuthenticationError(errorCode: Int, errString: CharSequence) {
                    if (continuation.isActive) {
                        continuation.resume(false)
                    }
                }

                override fun onAuthenticationFailed() {
                    // Keep the system prompt open until success or a terminal error.
                }
            },
        )
        continuation.invokeOnCancellation {
            prompt.cancelAuthentication()
        }
        prompt.authenticate(
            BiometricPrompt.PromptInfo.Builder()
                .setTitle(title)
                .setSubtitle(subtitle)
                .setAllowedAuthenticators(authenticators)
                .build(),
        )
    }
}

private fun Context.appLockAuthenticatorsOrNull(): Int? {
    val manager = BiometricManager.from(applicationContext)
    val candidates = buildList {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            add(Authenticators.BIOMETRIC_STRONG or Authenticators.DEVICE_CREDENTIAL)
        }
        add(Authenticators.BIOMETRIC_WEAK or Authenticators.DEVICE_CREDENTIAL)
        add(Authenticators.DEVICE_CREDENTIAL)
    }
    candidates.firstOrNull { authenticators ->
        manager.canAuthenticate(authenticators) == BiometricManager.BIOMETRIC_SUCCESS
    }?.let { return it }

    val keyguard = applicationContext.getSystemService(KeyguardManager::class.java)
    return if (keyguard?.isDeviceSecure == true) {
        Authenticators.DEVICE_CREDENTIAL
    } else {
        null
    }
}
