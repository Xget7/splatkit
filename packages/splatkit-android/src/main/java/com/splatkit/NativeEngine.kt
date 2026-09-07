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

    /** Receives [Event]s on the thread that raised them: loader or render thread. */
    var onEvent: ((Event, String, Int) -> Unit)? = null

    enum class Event { WORLD_READY, WORLD_FAILED, COLLIDER_READY, COLLIDER_FAILED }

    // Called from JNI; the name and signature are part of the native contract.
    @Suppress("unused")
    private fun onNativeEvent(kind: Int, message: String, splatCount: Int) {
        val event = Event.values().getOrNull(kind) ?: return
        onEvent?.invoke(event, message, splatCount)
    }

    fun setSurface(surface: Surface?) = nativeSetSurface(handle, surface)
    fun surfaceResized(width: Int, height: Int) = nativeSurfaceResized(handle, width, height)

    fun render(frameTimeNanos: Long) = nativeRender(handle, frameTimeNanos)

    fun loadWorld(spzBytes: ByteArray) = nativeLoadWorld(handle, spzBytes)

    fun loadCollider(glbBytes: ByteArray) = nativeLoadCollider(handle, glbBytes)

    fun loadWorldFile(path: String) = nativeLoadWorldFile(handle, path)

    fun loadColliderFile(path: String) = nativeLoadColliderFile(handle, path)

    fun setCameraPose(x: Float, y: Float, z: Float, yaw: Float, pitch: Float) =
        nativeSetCameraPose(handle, x, y, z, yaw, pitch)

    /** Fills [out] (at least 5) with x, y, z, yaw, pitch. Any thread. */
    fun cameraPose(out: FloatArray) = nativeCameraPose(handle, out)

    fun look(deltaYaw: Float, deltaPitch: Float) = nativeLook(handle, deltaYaw, deltaPitch)

    fun walk(forward: Float, right: Float) = nativeWalk(handle, forward, right)

    fun setAttitude(rowMajor: FloatArray) = nativeSetAttitude(handle, rowMajor)

    fun setMotionEnabled(enabled: Boolean) = nativeSetMotionEnabled(handle, enabled)

    fun setVelocity(forward: Float, right: Float) = nativeSetVelocity(handle, forward, right)

    fun startBenchmark(seconds: Float) = nativeStartBenchmark(handle, seconds)

    fun setRenderScale(scale: Float) = nativeSetRenderScale(handle, scale)

    fun setMaxShDegree(degree: Int) = nativeSetMaxShDegree(handle, degree)

    fun setSplatBudget(budget: Int) = nativeSetSplatBudget(handle, budget)

    fun setLinearBlending(linear: Boolean) = nativeSetLinearBlending(handle, linear)

    fun setCullMargin(degrees: Float) = nativeSetCullMargin(handle, degrees)

    /** Safe from any thread once created. */
    fun gpuDescription(): String = nativeGpuDescription(handle)

    /** Safe from any thread: the engine publishes these atomically. */
    fun stats(out: FloatArray) = nativeStats(handle, out)

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
    private external fun nativeSetVelocity(handle: Long, forward: Float, right: Float)
    private external fun nativeStartBenchmark(handle: Long, seconds: Float)
    private external fun nativeSetRenderScale(handle: Long, scale: Float)
    private external fun nativeSetMaxShDegree(handle: Long, degree: Int)
    private external fun nativeSetSplatBudget(handle: Long, budget: Int)
    private external fun nativeSetLinearBlending(handle: Long, linear: Boolean)
    private external fun nativeSetCullMargin(handle: Long, degrees: Float)
    private external fun nativeLoadWorldFile(handle: Long, path: String)
    private external fun nativeLoadColliderFile(handle: Long, path: String)
    private external fun nativeSetCameraPose(handle: Long, x: Float, y: Float, z: Float, yaw: Float, pitch: Float)
    private external fun nativeCameraPose(handle: Long, out: FloatArray)
    private external fun nativeGpuDescription(handle: Long): String
    private external fun nativeStats(handle: Long, out: FloatArray)

    companion object {
        init {
            System.loadLibrary("splatkit")
        }
    }
}
