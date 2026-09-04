package com.splatkit

import android.content.Context
import android.util.AttributeSet
import android.util.Log
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import kotlin.math.abs

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
private const val TAG = "SplatKit"

class SplatSurfaceView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : SurfaceView(context, attrs), SurfaceHolder.Callback {

    private val renderThread = RenderThread()
    private val motion = MotionInput(context, renderThread.renderHandler) { renderThread.setAttitude(it) }
    private var motionEnabled = false
    private var resumed = false

    private var lastX = 0f
    private var lastY = 0f
    private var lastPointerCount = 0
    private var lastTapTime = 0L

    /** Radians per pixel dragged. */
    var lookSensitivity = 0.004f
    /** Meters per pixel dragged with two fingers. */
    var walkSensitivity = 0.01f

    init {
        holder.addCallback(this)
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

    var listener: Listener?
        get() = renderThread.listener
        set(value) { renderThread.listener = value }

    /** False when Vulkan could not be brought up on this device; the view stays blank. */
    val isAvailable: Boolean get() = renderThread.isAvailable

    /** Decodes and shows an SPZ world. Replaces the current one when ready. */
    fun loadWorld(spzBytes: ByteArray) = renderThread.loadWorld(spzBytes)

    /** Decodes a collider GLB; enables walk mode when ready. */
    fun loadCollider(glbBytes: ByteArray) = renderThread.loadCollider(glbBytes)

    /**
     * Fraction of the view's resolution the splats are drawn at, in (0, 1]. Below one the
     * frame is drawn smaller and upscaled. Frame time scales almost directly with it,
     * because splat rendering is bound by blended fragments.
     */
    var renderScale: Float = 1f
        set(value) {
            field = value.coerceIn(0.1f, 1f)
            renderThread.setRenderScale(field)
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
     * Highest spherical harmonics degree kept from the file, 0 to 3, applied to worlds
     * loaded after it is set. Spherical harmonics make colour depend on the view
     * direction (highlights, sheen); World Labs worlds carry none, files from the
     * reference 3DGS pipeline carry degree 3, which costs 92 bytes per splat of GPU memory.
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
    fun readStats(into: SplatStats = SplatStats()): SplatStats {
        renderThread.stats(statsScratch)
        into.fps = statsScratch[0]
        into.frameMillis = statsScratch[1]
        into.sortMillis = statsScratch[2]
        into.splatCount = statsScratch[3].toInt()
        into.walking = statsScratch[4] != 0f
        into.motion = statsScratch[5] != 0f
        into.gpuMillis = statsScratch[6]
        return into
    }
    private val statsScratch = FloatArray(7)

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

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val count = event.pointerCount
        val x = (0 until count).sumOf { event.getX(it).toDouble() }.toFloat() / count
        val y = (0 until count).sumOf { event.getY(it).toDouble() }.toFloat() / count
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val now = event.eventTime
                if (now - lastTapTime < 300) setMotionEnabled(!motionEnabled)
                lastTapTime = now
            }
            MotionEvent.ACTION_MOVE -> if (count == lastPointerCount) {
                val dx = x - lastX
                val dy = y - lastY
                if (count == 1) {
                    renderThread.look(-dx * lookSensitivity, -dy * lookSensitivity)
                } else if (abs(dx) + abs(dy) > 0f) {
                    renderThread.walk(-dy * walkSensitivity, dx * walkSensitivity)
                }
            }
        }
        lastX = x
        lastY = y
        lastPointerCount = if (event.actionMasked == MotionEvent.ACTION_UP) 0 else count
        return true
    }
}
