package com.splatkit.input

import android.view.MotionEvent
import kotlin.math.abs

/**
 * The view's gestures: one finger drags the view (yaw, and pitch when the gyroscope is
 * off), two fingers walk (up is forward, sideways strafes), and a double tap toggles the
 * gyroscope. Pixels become radians and meters through the sensitivities.
 */
internal class TouchInput(private val listener: Listener) {
    interface Listener {
        fun onLook(deltaYaw: Float, deltaPitch: Float)
        fun onWalk(forward: Float, right: Float)
        fun onDoubleTap()
    }

    /** Radians per pixel dragged. */
    var lookSensitivity = 0.004f

    /** Meters per pixel dragged with two fingers. */
    var walkSensitivity = 0.01f

    private var lastX = 0f
    private var lastY = 0f
    private var lastPointerCount = 0
    private var lastTapTime = 0L

    fun onTouchEvent(event: MotionEvent): Boolean {
        val count = event.pointerCount
        val x = (0 until count).sumOf { event.getX(it).toDouble() }.toFloat() / count
        val y = (0 until count).sumOf { event.getY(it).toDouble() }.toFloat() / count
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val now = event.eventTime
                if (now - lastTapTime < DOUBLE_TAP_MILLIS) listener.onDoubleTap()
                lastTapTime = now
            }
            MotionEvent.ACTION_MOVE -> if (count == lastPointerCount) {
                val dx = x - lastX
                val dy = y - lastY
                if (count == 1) {
                    listener.onLook(-dx * lookSensitivity, -dy * lookSensitivity)
                } else if (abs(dx) + abs(dy) > 0f) {
                    listener.onWalk(-dy * walkSensitivity, dx * walkSensitivity)
                }
            }
        }
        lastX = x
        lastY = y
        lastPointerCount = if (event.actionMasked == MotionEvent.ACTION_UP) 0 else count
        return true
    }

    private companion object {
        const val DOUBLE_TAP_MILLIS = 300L
    }
}
