package com.splatkit

import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.view.Choreographer
import android.view.Surface
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
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
    private var engine: NativeEngine? = null
    private var rendering = false

    /** GPU name and Vulkan version, or an empty string when the engine failed to start. */
    val gpuDescription: String

    // Decoding runs here so frames keep flowing; the engine uploads on its next frame.
    private val loader = Executors.newSingleThreadExecutor { Thread(it, "SplatKitLoader") }

    init {
        // Everything that must live on the render thread is created there.
        runBlockingOnThread {
            choreographer = Choreographer.getInstance()
            engine = NativeEngine()
        }
        gpuDescription = engine?.gpuDescription() ?: ""
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

    fun loadWorld(spzBytes: ByteArray) {
        loader.execute { engine?.loadWorld(spzBytes) }
    }

    fun loadCollider(glbBytes: ByteArray) {
        loader.execute { engine?.loadCollider(glbBytes) }
    }

    fun look(deltaYaw: Float, deltaPitch: Float) = post { engine?.look(deltaYaw, deltaPitch) }

    fun walk(forward: Float, right: Float) = post { engine?.walk(forward, right) }

    fun setAttitude(rowMajor: FloatArray) = post { engine?.setAttitude(rowMajor) }

    fun setMotionEnabled(enabled: Boolean) = post { engine?.setMotionEnabled(enabled) }

    fun setVelocity(forward: Float, right: Float) = post { engine?.setVelocity(forward, right) }

    fun startBenchmark(seconds: Float) = post { engine?.startBenchmark(seconds) }

    fun setRenderScale(scale: Float) = post { engine?.setRenderScale(scale) }

    /** Reads the latest stats into [out]; see [NativeEngine.stats] for the layout. */
    fun stats(out: FloatArray) {
        engine?.stats(out)
    }

    /** Handler on the render thread, for listeners that should deliver there. */
    val renderHandler: Handler get() = handler

    fun release() {
        // A decode still running would touch engine state while it is destroyed, so this
        // never proceeds until the loader has really stopped.
        loader.shutdown()
        while (!loader.awaitTermination(10, TimeUnit.SECONDS)) {
            Log.w(TAG, "waiting for a world decode to finish before releasing the engine")
        }
        runBlockingOnThread {
            rendering = false
            choreographer.removeFrameCallback(frameCallback)
            engine?.destroy()
            engine = null
        }
        thread.quitSafely()
        thread.join()
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
