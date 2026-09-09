package com.splatkit

import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.util.Log
import android.view.Choreographer
import android.view.Surface
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.TimeUnit

/**
 * Owns the native engine and drives it from a dedicated thread, one frame per vsync.
 *
 * The UI thread never touches the engine. Surface changes are posted here; the ones
 * that must complete before Android reclaims the surface block the caller until done.
 * The render thread never waits on the UI thread, so that cannot deadlock.
 */
internal class RenderThread {
    private val thread = HandlerThread("SplatKitRender").apply { start() }
    private val handler = Handler(thread.looper)
    private lateinit var choreographer: Choreographer
    // Written on the render thread, read by the any-thread getters (stats, camera pose).
    @Volatile private var engine: NativeEngine? = null
    private var rendering = false

    /** GPU name and Vulkan version, or an empty string when the engine failed to start. */
    val gpuDescription: String

    /** False when Vulkan could not be brought up; every call is then a no-op. */
    val isAvailable: Boolean

    /** Delivered on the main thread. */
    var listener: SplatSurfaceView.Listener? = null
    private val mainHandler = Handler(Looper.getMainLooper())

    // Decoding runs here so frames keep flowing; the engine uploads on its next frame.
    private val loader = Executors.newSingleThreadExecutor { Thread(it, "SplatKitLoader") }

    init {
        // Everything that must live on the render thread is created there.
        runBlockingOnThread {
            choreographer = Choreographer.getInstance()
            engine = NativeEngine()
        }
        gpuDescription = engine?.gpuDescription() ?: ""
        isAvailable = engine?.isValid == true
        engine?.onEvent = { event, message, splatCount ->
            mainHandler.post {
                val l = listener ?: return@post
                when (event) {
                    NativeEngine.Event.WORLD_READY -> l.onWorldReady(splatCount)
                    NativeEngine.Event.WORLD_FAILED -> l.onWorldFailed(message)
                    NativeEngine.Event.COLLIDER_READY -> l.onColliderReady()
                    NativeEngine.Event.COLLIDER_FAILED -> l.onColliderFailed(message)
                }
            }
        }
    }

    private val frameCallback = object : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            if (!rendering) return
            engine?.render(frameTimeNanos)
            choreographer.postFrameCallback(this)
        }
    }

    fun surfaceCreated(surface: Surface) = runBlockingOnThread {
        engine?.setSurface(surface)
    }

    /** The surface kept its identity but changed size, typically a rotation. */
    fun surfaceResized(width: Int, height: Int) = post {
        engine?.surfaceResized(width, height)
    }

    /** Blocks: after this returns Android may destroy the surface. */
    fun surfaceDestroyed() = runBlockingOnThread {
        engine?.setSurface(null)
    }

    fun resume() = post {
        if (!rendering) {
            rendering = true
            choreographer.postFrameCallback(frameCallback)
        }
    }

    fun pause() = runBlockingOnThread {
        rendering = false
        choreographer.removeFrameCallback(frameCallback)
    }

    fun loadWorld(spzBytes: ByteArray) = decode { it.loadWorld(spzBytes) }

    fun loadCollider(glbBytes: ByteArray) = decode { it.loadCollider(glbBytes) }

    fun loadWorldFile(path: String) = decode { it.loadWorldFile(path) }

    fun loadColliderFile(path: String) = decode { it.loadColliderFile(path) }

    // After release() the executor is shut down and would throw; a late load from a
    // host's background thread is a no-op like every other call after release.
    private fun decode(block: (NativeEngine) -> Unit) {
        if (loader.isShutdown) return
        try {
            loader.execute { engine?.let(block) }
        } catch (e: RejectedExecutionException) {
            Log.w(TAG, "load after release ignored")
        }
    }

    fun setCameraPose(pose: CameraPose) = post {
        engine?.setCameraPose(pose.x, pose.y, pose.z, pose.yaw, pose.pitch)
    }

    /** The pose as of the last frame, or null before the engine exists. Any thread. */
    fun cameraPose(out: FloatArray): Boolean {
        val e = engine ?: return false
        e.cameraPose(out)
        return true
    }

    fun look(deltaYaw: Float, deltaPitch: Float) = post { engine?.look(deltaYaw, deltaPitch) }

    fun walk(forward: Float, right: Float) = post { engine?.walk(forward, right) }

    fun setAttitude(rowMajor: FloatArray) = post { engine?.setAttitude(rowMajor) }

    fun setMotionEnabled(enabled: Boolean) = post { engine?.setMotionEnabled(enabled) }

    fun setVelocity(forward: Float, right: Float) = post { engine?.setVelocity(forward, right) }

    fun startBenchmark(seconds: Float) = post { engine?.startBenchmark(seconds) }

    fun setRenderScale(scale: Float) = post { engine?.setRenderScale(scale) }

    fun setMaxShDegree(degree: Int) = post { engine?.setMaxShDegree(degree) }
    fun setShDegree(degree: Int) = post { engine?.setShDegree(degree) }

    fun setSplatBudget(budget: Int) = post { engine?.setSplatBudget(budget) }

    fun setLinearBlending(linear: Boolean) = post { engine?.setLinearBlending(linear) }

    fun setCullMargin(degrees: Float) = post { engine?.setCullMargin(degrees) }

    /** Reads the latest stats into [out]; see [NativeEngine.stats] for the layout. */
    fun stats(out: FloatArray) {
        engine?.stats(out)
    }

    /** Handler on the render thread, for listeners that should deliver there. */
    val renderHandler: Handler get() = handler

    /**
     * Stops rendering now and destroys the engine once any decode in flight has finished.
     * A decode touches engine state, so the destruction is queued behind it on the loader
     * instead of blocking the caller: a host calling this from onDestroy must not wait on
     * a slow file, and the engine must not die under a running decoder.
     */
    fun release() {
        runBlockingOnThread {
            rendering = false
            choreographer.removeFrameCallback(frameCallback)
        }
        if (loader.isShutdown) return
        loader.execute {
            runBlockingOnThread {
                engine?.destroy()
                engine = null
            }
            thread.quitSafely()
        }
        loader.shutdown()
    }

    private fun post(block: () -> Unit) {
        handler.post(block)
    }

    private fun runBlockingOnThread(block: () -> Unit) {
        if (Thread.currentThread() === thread) {
            block()
            return
        }
        val done = CountDownLatch(1)
        handler.post {
            try {
                block()
            } finally {
                done.countDown()
            }
        }
        // The framework calls surfaceDestroyed on the UI thread; a render thread stuck in
        // the driver must not turn into an ANR. Frame waits are bounded, so this is the
        // last line of defence and is logged loudly.
        if (!done.await(5, TimeUnit.SECONDS)) {
            Log.e(TAG, "render thread did not respond within 5 s")
        }
    }

    private companion object {
        const val TAG = "SplatKit"
    }
}
