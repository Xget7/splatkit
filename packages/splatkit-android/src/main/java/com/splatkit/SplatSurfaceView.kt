package com.splatkit

import android.content.Context
import android.util.AttributeSet
import android.util.Log
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import com.splatkit.engine.NativeEngine
import com.splatkit.engine.RenderThread
import com.splatkit.input.MotionInput
import com.splatkit.input.TouchInput
import java.io.File

private const val TAG = "SplatKit"

/**
 * A SurfaceView that renders with SplatKit.
 *
 * The host activity forwards resume, pause and release. Everything else follows the
 * surface lifecycle: the engine gets the surface when Android creates it and gives it
 * back, synchronously, before Android destroys it.
 *
 * Gestures: one finger drags the view (yaw, and pitch when the gyroscope is off);
 * two fingers walk (up is forward, sideways strafes); a double tap toggles the gyroscope.
 */
class SplatSurfaceView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : SurfaceView(context, attrs), SurfaceHolder.Callback {

    private val renderThread = RenderThread()
    private val motion = MotionInput(context, renderThread.renderHandler) { renderThread.setAttitude(it) }
    private val touch = TouchInput(object : TouchInput.Listener {
        override fun onLook(deltaYaw: Float, deltaPitch: Float) = renderThread.look(deltaYaw, deltaPitch)
        override fun onWalk(forward: Float, right: Float) = renderThread.walk(forward, right)
        override fun onDoubleTap() = setMotionEnabled(!motionEnabled)
    })
    private var motionEnabled = false
    private var resumed = false

    /** Radians per pixel dragged. */
    var lookSensitivity: Float
        get() = touch.lookSensitivity
        set(value) { touch.lookSensitivity = value }

    /** Meters per pixel dragged with two fingers. */
    var walkSensitivity: Float
        get() = touch.walkSensitivity
        set(value) { touch.walkSensitivity = value }

    init {
        holder.addCallback(this)
        renderThread.onEvent = { event, message, splatCount ->
            val l = listener
            if (l != null) when (event) {
                NativeEngine.Event.WORLD_READY -> l.onWorldReady(splatCount)
                NativeEngine.Event.WORLD_FAILED -> l.onWorldFailed(message)
                NativeEngine.Event.COLLIDER_READY -> l.onColliderReady()
                NativeEngine.Event.COLLIDER_FAILED -> l.onColliderFailed(message)
            }
        }
    }

    /** Loading outcomes, delivered on the main thread. All methods have empty defaults. */
    interface Listener {
        /** The world is uploaded and drawing. */
        fun onWorldReady(splatCount: Int) {}
        /** The bytes were not a readable SPZ, or the GPU refused them; the previous world stays. */
        fun onWorldFailed(message: String) {}
        /** Walk mode is on. */
        fun onColliderReady() {}
        fun onColliderFailed(message: String) {}
    }

    var listener: Listener? = null

    /** False when Vulkan could not be brought up on this device; the view stays blank. */
    val isAvailable: Boolean get() = renderThread.isAvailable

    /** Decodes and shows an SPZ world. Replaces the current one when ready. */
    fun loadWorld(spzBytes: ByteArray) = renderThread.loadWorld(spzBytes)

    /** Decodes a collider GLB; enables walk mode when ready. */
    fun loadCollider(glbBytes: ByteArray) = renderThread.loadCollider(glbBytes)

    /**
     * Decodes and shows a world from a file the app can read. The file is mapped, not
     * copied through the Java heap, so this is the way to load big worlds.
     */
    fun loadWorld(file: File) = renderThread.loadWorldFile(file.absolutePath)

    /** Decodes a collider GLB from a file; enables walk mode when ready. */
    fun loadCollider(file: File) = renderThread.loadColliderFile(file.absolutePath)

    /**
     * The camera's position and look direction. Reading gives the pose as of the last
     * frame; setting teleports, and when walking the camera settles on the floor under
     * the new point on the next frame. Any thread.
     */
    var cameraPose: CameraPose
        get() = renderThread.cameraPose() ?: CameraPose(0f, 0f, 0f)
        set(value) = renderThread.setCameraPose(value)

    /**
     * Applies a [RenderQuality] preset by setting [renderScale], [shDegree],
     * [splatBudget], [linearBlending] and [cullMarginDegrees] from it. Set any of them
     * afterwards to depart from the preset. Everything but [splatBudget] takes effect on
     * the next frame; the budget reaches worlds loaded after the call.
     */
    fun applyQuality(quality: RenderQuality) {
        renderScale = quality.renderScale
        shDegree = quality.shDegree
        splatBudget = quality.splatBudget
        linearBlending = quality.linearBlending
        cullMarginDegrees = quality.cullMarginDegrees
    }

    /**
     * Fraction of the view's resolution the splats are drawn at, in [0.1, 2]. Below one
     * the frame is drawn smaller and upscaled; frame time scales almost directly with it,
     * because splat rendering is bound by blended fragments. Above one the frame is
     * supersampled and downscaled, which steadies thin splats that shimmer at about a
     * pixel each, for about the square of the scale in frame time.
     */
    var renderScale: Float = 1f
        set(value) {
            field = value.coerceIn(0.1f, 2f)
            renderThread.setRenderScale(field)
        }

    /**
     * Angular margin around the view, in degrees, kept drawn so that what turns into
     * view before the next cull lands is already there; the engine widens it further
     * during a fast turn. 10 by default. Wider draws more that is off screen, narrower
     * risks an empty edge on a flick.
     */
    var cullMarginDegrees: Float = 10f
        set(value) {
            field = value.coerceIn(0f, 80f)
            renderThread.setCullMargin(field)
        }

    /**
     * Blend splats in linear light instead of the encoded colour space. The reference
     * rasterizer, and so the training, blend encoded values; linear blending gives
     * richer contrast that the training never saw and costs 40% of the frame on
     * Adreno 640. Off by default.
     */
    var linearBlending: Boolean = false
        set(value) {
            field = value
            renderThread.setLinearBlending(value)
        }

    /**
     * Most splats drawn per frame, or 0 to draw them all. With a budget, a world loaded
     * afterwards gets a level of detail hierarchy (about 1.5 times the splats in GPU
     * memory) and every frame draws the nodes that cover the scene at about a pixel
     * each, nearest in full detail, so frame time stops depending on the scene's size.
     * Applies to worlds loaded after it is set.
     */
    var splatBudget: Int = 0
        set(value) {
            field = value.coerceAtLeast(0)
            renderThread.setSplatBudget(field)
        }

    /**
     * Spherical harmonics degree drawn, 0 to 3, capped by what the loaded world carries.
     * Takes effect on the next frame. Spherical harmonics make colour depend on the view
     * direction (highlights, sheen, the glint on water and leaves); 0 draws the base
     * colour only, which is what World Labs worlds carry anyway.
     */
    var shDegree: Int = 3
        set(value) {
            field = value.coerceIn(0, 3)
            renderThread.setShDegree(field)
        }

    /**
     * Highest spherical harmonics degree kept in GPU memory from the file, 0 to 3,
     * applied to worlds loaded after it is set. A memory cap, not a quality setting:
     * degree 3 costs 92 bytes per splat, so a host on a small phone can lower it before
     * [loadWorld] and [shDegree] then cannot go above it for that world.
     */
    var maxShDegree: Int = 3
        set(value) {
            field = value.coerceIn(0, 3)
            renderThread.setMaxShDegree(field)
        }

    /** Walks continuously at the given speed in meters per second until called again with zeros. */
    fun setWalkVelocity(forward: Float, right: Float) = renderThread.setVelocity(forward, right)

    /**
     * Runs a reproducible capture: the gyroscope goes off, the camera takes a fixed pose
     * and turns once over [seconds], then the frame time distribution is logged.
     * Starts as soon as a world is loaded.
     */
    fun startBenchmark(seconds: Float = 10f) {
        setMotionEnabled(false)
        renderThread.startBenchmark(seconds)
    }

    /** GPU name and Vulkan version reported by the driver. */
    val gpuDescription: String get() = renderThread.gpuDescription

    /** True while the gyroscope drives the camera. */
    val isMotionEnabled: Boolean get() = motionEnabled

    /** Latest engine stats. Cheap; safe on the UI thread. */
    fun readStats(into: SplatStats = SplatStats()): SplatStats = renderThread.readStats(into)

    /** Drives the camera with the phone's orientation. No-op when the sensor is missing. */
    fun setMotionEnabled(enabled: Boolean) {
        motionEnabled = enabled && motion.isAvailable
        renderThread.setMotionEnabled(motionEnabled)
        if (resumed) {
            if (motionEnabled) motion.start() else motion.stop()
        }
    }

    fun resume() {
        resumed = true
        motion.displayRotation = display?.rotation ?: 0
        renderThread.resume()
        if (motionEnabled) motion.start()
    }

    fun pause() {
        resumed = false
        motion.stop()
        renderThread.pause()
    }

    fun release() {
        motion.stop()
        holder.removeCallback(this)
        renderThread.release()
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        motion.displayRotation = display?.rotation ?: 0
        renderThread.surfaceCreated(holder.surface)
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        // Real drivers also report the change through the swapchain; the emulator does not,
        // so the size travels explicitly and the engine rebuilds when it differs.
        Log.i(TAG, "surface changed to ${width}x${height}, display rotation ${display?.rotation}")
        motion.displayRotation = display?.rotation ?: 0
        renderThread.surfaceResized(width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        renderThread.surfaceDestroyed()
    }

    override fun onTouchEvent(event: MotionEvent): Boolean = touch.onTouchEvent(event)
}
