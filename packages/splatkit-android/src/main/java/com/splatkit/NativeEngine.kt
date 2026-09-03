package com.splatkit

import android.view.Surface

/**
 * Thin JNI boundary to the C++ engine. Every call happens on the render thread except
 * [loadWorld], which decodes on the calling thread and is safe to call from a loader.
 * The handle is an opaque pointer owned by native code.
 */
internal class NativeEngine {
    private var handle: Long = nativeCreate()

    val isValid: Boolean get() = handle != 0L

    fun setSurface(surface: Surface?) = nativeSetSurface(handle, surface)
    fun surfaceResized(width: Int, height: Int) = nativeSurfaceResized(handle, width, height)

    fun render(frameTimeNanos: Long) = nativeRender(handle, frameTimeNanos)

    fun loadWorld(spzBytes: ByteArray) = nativeLoadWorld(handle, spzBytes)

    fun loadCollider(glbBytes: ByteArray) = nativeLoadCollider(handle, glbBytes)

    fun look(deltaYaw: Float, deltaPitch: Float) = nativeLook(handle, deltaYaw, deltaPitch)

    fun walk(forward: Float, right: Float) = nativeWalk(handle, forward, right)

    fun setAttitude(rowMajor: FloatArray) = nativeSetAttitude(handle, rowMajor)

    fun setMotionEnabled(enabled: Boolean) = nativeSetMotionEnabled(handle, enabled)

    fun destroy() {
        if (handle != 0L) {
            nativeDestroy(handle)
            handle = 0L
        }
    }

    private external fun nativeCreate(): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeSetSurface(handle: Long, surface: Surface?)
    private external fun nativeSurfaceResized(handle: Long, width: Int, height: Int)
    private external fun nativeRender(handle: Long, frameTimeNanos: Long)
    private external fun nativeLoadWorld(handle: Long, spzBytes: ByteArray)
    private external fun nativeLoadCollider(handle: Long, glbBytes: ByteArray)
    private external fun nativeLook(handle: Long, deltaYaw: Float, deltaPitch: Float)
    private external fun nativeWalk(handle: Long, forward: Float, right: Float)
    private external fun nativeSetAttitude(handle: Long, rowMajor: FloatArray)
    private external fun nativeSetMotionEnabled(handle: Long, enabled: Boolean)

    companion object {
        init {
            System.loadLibrary("splatkit")
        }
    }
}
