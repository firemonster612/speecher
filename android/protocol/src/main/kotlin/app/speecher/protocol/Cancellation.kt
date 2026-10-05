package app.speecher.protocol

import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Lets another thread abort a provider operation, its retries included: [cancel] closes the request
 * in flight, so a blocked read ends at once, and fails every request started after it as Cancelled.
 */
class Cancellation {
    private var cancelled = false
    private var abort: (() -> Unit)? = null

    fun cancel() {
        val inFlight =
            synchronized(this) {
                cancelled = true
                abort
            }
        inFlight?.invoke()
    }

    /**
     * Runs one request whose connection [abort] closes. Past [deadlineNanos], on the
     * [System.nanoTime] clock, it is closed as well and fails as a Timeout, so retries given the
     * same deadline share one budget, and one started after it is never sent.
     */
    internal fun <T> request(abort: () -> Unit, deadlineNanos: Long? = null, block: () -> T): T {
        synchronized(this) {
            if (cancelled) throw cancelledFailure()
            if (deadlineNanos != null && System.nanoTime() >= deadlineNanos) throw deadlineFailure()
            this.abort = abort
        }
        val expired = AtomicBoolean(false)
        val timer = deadlineNanos?.let {
            deadlines.schedule(
                {
                    expired.set(true)
                    abort()
                },
                it - System.nanoTime(),
                TimeUnit.NANOSECONDS,
            )
        }
        try {
            return block()
        } catch (error: Exception) {
            if (synchronized(this) { cancelled }) throw cancelledFailure(error)
            if (expired.get()) throw deadlineFailure(error)
            throw error
        } finally {
            timer?.cancel(false)
            synchronized(this) { this.abort = null }
        }
    }
}

private fun cancelledFailure(cause: Throwable? = null) =
    ProviderFailure(ProviderFailureKind.Cancelled, "Cancelled", cause = cause)

private fun deadlineFailure(cause: Throwable? = null) =
    ProviderFailure(
        ProviderFailureKind.Timeout,
        "No complete answer before the deadline",
        cause = cause,
    )

private val deadlines = Executors.newSingleThreadScheduledExecutor { task ->
    Thread(task, "provider-deadline").apply { isDaemon = true }
}
