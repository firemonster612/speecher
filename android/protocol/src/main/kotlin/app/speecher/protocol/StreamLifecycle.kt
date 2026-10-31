package app.speecher.protocol

import java.util.concurrent.Executors
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit

/** Audio held while a stream is not live yet, over which the stream fails. */
private const val MAX_PENDING_BYTES = 4 * 1024 * 1024

/**
 * The lifecycle the streaming speech clients share: audio held until the provider accepts it, the
 * deadlines for going live and for closing, and the one Completed or Failed event that ends a
 * stream, after which it reports nothing more. A client supplies how its provider takes audio
 * ([sendChunk]) and is asked to finish ([requestClose]), and hands its transport callbacks here.
 */
internal class StreamLifecycle(
    private val transport: WebSocketTransport,
    private val events: (SpeechEvent) -> Unit,
    private val sendChunk: (ByteArray) -> Unit,
    private val requestClose: () -> Unit,
    /** How long the provider has to end the stream once asked to close. */
    private val closeTimeoutMillis: Long,
) {
    private val lock = Any()
    private val pending = mutableListOf<ByteArray>()
    private var pendingBytes = 0
    /** The wait for the provider to close, once asked; guarded by [lock]. */
    private var closeWait: ScheduledFuture<*>? = null
    /** Whether the provider takes audio now: its socket opened, or its session started. */
    @Volatile
    var live = false
        private set

    /** Whether [stop] ran: no more audio comes, and the stream ends once the provider closes it. */
    @Volatile
    var stopped = false
        private set

    @Volatile private var cancelled = false
    /** Whether Completed or Failed was reported. */
    @Volatile private var finished = false
    private val timer = Executors.newSingleThreadScheduledExecutor { task ->
        Thread(task, "speech-stream-timer").apply { isDaemon = true }
    }

    /**
     * Whether the stream was cancelled or reported its end; the provider's events no longer count.
     */
    val ended: Boolean
        get() = cancelled || finished

    /** Fails the stream with a timeout unless it is live [seconds] from now. */
    fun failUnlessLiveWithin(seconds: Long, detail: String, retryable: Boolean = false) {
        schedule(TimeUnit.SECONDS.toMillis(seconds)) {
            if (!live) fail(ProviderFailureKind.Timeout, detail, retryable)
        }
    }

    /** Runs [task] every [seconds] until the stream ends. */
    fun repeat(seconds: Long, task: () -> Unit) {
        try {
            timer.scheduleAtFixedRate({ if (!ended) task() }, seconds, seconds, TimeUnit.SECONDS)
        } catch (_: RejectedExecutionException) {}
    }

    /**
     * Sends [pcm] now if live, else holds it for [goLive]; the stream fails if too much is held.
     */
    fun sendAudio(pcm: ByteArray) {
        if (stopped || ended || pcm.isEmpty()) return
        synchronized(lock) {
            if (live) {
                sendChunk(pcm)
                return
            }
            if (pendingBytes + pcm.size <= MAX_PENDING_BYTES) {
                pending.add(pcm)
                pendingBytes += pcm.size
                return
            }
        }
        fail(ProviderFailureKind.Network, "audio buffer overflow")
    }

    /**
     * The provider takes audio from now on: sends what was held, then closes the stream if it was
     * stopped meanwhile, or reports Connected. Only the first call counts.
     */
    fun goLive() {
        // Flushed under the lock so audio captured meanwhile queues behind it, not ahead.
        synchronized(lock) {
            if (live) return
            live = true
            pending.forEach(sendChunk)
            pending.clear()
            pendingBytes = 0
        }
        if (stopped) close() else events(SpeechEvent.Connected)
    }

    /**
     * Ends the audio: asks the provider to close now if live, else once it goes live. False when
     * the stream was already stopped or has ended, and nothing happened.
     */
    fun stop(): Boolean {
        if (stopped || ended) return false
        stopped = true
        if (live) close()
        return true
    }

    private fun close() {
        requestClose()
        synchronized(lock) { closeWait = waitForClose() }
    }

    /**
     * Restarts the wait for the provider to close, if it is waiting: a provider still sending
     * results is finishing, not stuck, as the desktop's ChatGPT client treats it.
     */
    fun extendCloseWait() =
        synchronized(lock) { if (closeWait?.cancel(false) == true) closeWait = waitForClose() }

    private fun waitForClose() =
        schedule(closeTimeoutMillis) {
            fail(ProviderFailureKind.Timeout, "no close in ${closeTimeoutMillis / 1000}s")
        }

    fun cancel() {
        cancelled = true
        timer.shutdownNow()
        synchronized(lock) {
            pending.clear()
            pendingBytes = 0
        }
        transport.cancel()
    }

    /** The transport failed: a refused upgrade, a dropped socket, or a send it could not queue. */
    fun onFailure(error: Throwable, statusCode: Int?) {
        val detail =
            listOfNotNull(statusCode?.let { "HTTP $it" }, error.message?.takeIf { it.isNotBlank() })
                .joinToString(": ")
                .ifEmpty { "connect failed" }
        val kind = transportFailureKind(error, statusCode)
        fail(kind, detail, retryable = kind != ProviderFailureKind.Authentication)
    }

    fun onClosed(code: Int, reason: String) {
        if (live && !stopped && endsSession(code)) complete()
        else
            fail(
                ProviderFailureKind.Network,
                "closed $code ${reason.take(80)}".trim(),
                retryable = true,
            )
    }

    /** Reports Completed, unless the stream already ended. */
    fun complete() {
        if (!finish()) return
        events(SpeechEvent.Completed)
    }

    /** Reports the failure and drops the connection, unless the stream already ended. */
    fun fail(kind: ProviderFailureKind, detail: String, retryable: Boolean = false) {
        if (!finish()) return
        events(SpeechEvent.Failed(kind, detail, retryable))
        transport.cancel()
    }

    /** Marks the stream finished and stops its timers; false when it had ended already. */
    private fun finish(): Boolean {
        synchronized(lock) {
            if (ended) return false
            finished = true
        }
        timer.shutdownNow()
        return true
    }

    // A timer set as the stream ends finds the executor shut down; it has nothing left to time.
    private fun schedule(millis: Long, task: () -> Unit): ScheduledFuture<*>? =
        try {
            timer.schedule(task, millis, TimeUnit.MILLISECONDS)
        } catch (_: RejectedExecutionException) {
            null
        }
}
