// Copyright 2026, starseaN contributors
// SPDX-License-Identifier: GPL-3.0

package features.resources

import java.math.BigDecimal
import java.net.SocketException
import java.net.SocketTimeoutException
import java.net.UnknownHostException
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive

internal data class ResourceAutoUpdatePeriod(
    val months: Int = 0,
    val days: Int = 0,
    val hours: Int = 0,
    val minutes: Int = 0,
) {
    fun totalMillis(): Long {
        val daysFromMonths = months.toLong() * DaysPerMonth
        return (((daysFromMonths + days) * 24L + hours) * 60L + minutes) * 60_000L
    }
}

internal fun parseResourceAutoUpdateInterval(value: String): ResourceAutoUpdatePeriod? {
    val normalized = value.trim()
    if (normalized.isEmpty() || normalized.length > 64) return null
    if (EncodedPeriod.matches(normalized)) {
        val match = EncodedPeriod.matchEntire(normalized) ?: return null
        return ResourceAutoUpdatePeriod(
            months = match.groupValues[1].toIntOrNull() ?: 0,
            days = match.groupValues[2].toIntOrNull() ?: 0,
            hours = match.groupValues[3].toIntOrNull() ?: 0,
            minutes = match.groupValues[4].toIntOrNull() ?: 0,
        ).takeIf(::isValidResourceAutoUpdatePeriod)
    }
    val hours = normalized.replace(',', '.').toBigDecimalOrNull() ?: return null
    if (hours < MinimumHours || hours > MaximumHours) return null
    val totalMinutes = hours.multiply(MinutesPerHour).setScale(0, java.math.RoundingMode.HALF_UP).toLong()
    if (totalMinutes !in MinimumMinutes..MaximumMinutes) return null
    return ResourceAutoUpdatePeriod(
        hours = (totalMinutes / 60L).toInt(),
        minutes = (totalMinutes % 60L).toInt(),
    )
}

internal fun encodeResourceAutoUpdateInterval(period: ResourceAutoUpdatePeriod): String? {
    if (!isValidResourceAutoUpdatePeriod(period)) return null
    return buildString {
        if (period.months > 0) append(period.months).append('M')
        if (period.days > 0) append(period.days).append('D')
        if (period.hours > 0) append(period.hours).append('H')
        if (period.minutes > 0) append(period.minutes).append('m')
    }.ifEmpty { null }
}

internal fun resourceAutoUpdatePeriodFromFields(
    months: String,
    days: String,
    hours: String,
    minutes: String,
): ResourceAutoUpdatePeriod? {
    fun field(value: String): Int? {
        val trimmed = value.trim()
        if (trimmed.isEmpty()) return 0
        if (trimmed.length > 4 || trimmed.any { !it.isDigit() }) return null
        return trimmed.toIntOrNull()
    }
    return ResourceAutoUpdatePeriod(
        months = field(months) ?: return null,
        days = field(days) ?: return null,
        hours = field(hours) ?: return null,
        minutes = field(minutes) ?: return null,
    ).takeIf(::isValidResourceAutoUpdatePeriod)
}

internal fun isValidResourceAutoUpdatePeriod(period: ResourceAutoUpdatePeriod): Boolean {
    if (period.months < 0 || period.days < 0 || period.hours < 0 || period.minutes < 0) return false
    if (period.months > 12 || period.days > 366 || period.hours > 8760 || period.minutes > 59) return false
    val minutes = period.totalMillis() / 60_000L
    return minutes in MinimumMinutes..MaximumMinutes
}

internal fun resourceAutoUpdateIntervalMillis(enabled: Boolean, interval: String): Long? {
    if (!enabled) return null
    return parseResourceAutoUpdateInterval(interval)?.totalMillis()
}

private val EncodedPeriod = Regex("""^(?=.*[MDhm])(?:(\d+)M)?(?:(\d+)D)?(?:(\d+)H)?(?:(\d+)m)?$""")
private const val DaysPerMonth = 30L
private const val MinutesPerDay = 24L * 60L
private val MinutesPerHour = BigDecimal(60)
private val MinimumHours = BigDecimal("0.1")
private val MaximumHours = BigDecimal("8760")
private const val MinimumMinutes = 6L
private const val MaximumMinutes = 8760L * 60L

internal fun isTransientResourceUpdateFailure(error: Throwable): Boolean =
    generateSequence(error) { it.cause }.take(16).any { cause ->
        cause is SocketException || cause is SocketTimeoutException || cause is UnknownHostException ||
            HttpStatus.find(cause.message.orEmpty())?.groupValues?.get(1)?.toIntOrNull()?.let { code ->
                code == 408 || code == 429 || code in 500..599
            } == true
    }

private val HttpStatus = Regex("""(?i)\bHTTP\s+(\d{3})\b""")

internal enum class ResourceAutoUpdateOutcome { Success, Retry, Failed, Cancelled }

internal class ResourceAutoUpdateRunner(
    private val completed: (String) -> Boolean,
    private val record: (String) -> Unit,
    private val shouldContinue: () -> Boolean,
    private val attempts: (String) -> Int = { 0 },
    private val recordAttempt: (String) -> Unit = {},
    private val update: suspend (String) -> ResourceAutoUpdateOutcome,
) {
    suspend fun run(targets: List<String>): ResourceAutoUpdateOutcome {
        var retry = false
        var failed = false
        for (target in targets.sortedBy(attempts)) {
            currentCoroutineContext().ensureActive()
            if (!shouldContinue()) return ResourceAutoUpdateOutcome.Cancelled
            if (completed(target)) continue
            if (attempts(target) >= 3) {
                failed = true
                record(target)
                continue
            }
            recordAttempt(target)
            when (update(target)) {
                ResourceAutoUpdateOutcome.Success -> record(target)
                ResourceAutoUpdateOutcome.Failed -> {
                    failed = true
                    record(target)
                }
                ResourceAutoUpdateOutcome.Retry -> {
                    if (attempts(target) >= 3) {
                        failed = true
                        record(target)
                    } else retry = true
                }
                ResourceAutoUpdateOutcome.Cancelled -> return ResourceAutoUpdateOutcome.Cancelled
            }
        }
        return when {
            retry -> ResourceAutoUpdateOutcome.Retry
            failed -> ResourceAutoUpdateOutcome.Failed
            else -> ResourceAutoUpdateOutcome.Success
        }
    }
}
