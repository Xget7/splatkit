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

    /** Decodes and shows an SPZ world. Replaces the current one when ready. */
    fun loadWorld(spzBytes: ByteArray) = renderThread.loadWorld(spzBytes)

    /** Decodes a collider GLB; enables walk mode when ready. */
    fun loadCollider(glbBytes: ByteArray) = renderThread.loadCollider(glbBytes)

    /** Walks continuously at the given speed in meters per second until called again with zeros. */
    fun setWalkVelocity(forward: Float, right: Float) = renderThread.setVelocity(forward, right)

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
        return into
    }
    private val statsScratch = FloatArray(6)

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
