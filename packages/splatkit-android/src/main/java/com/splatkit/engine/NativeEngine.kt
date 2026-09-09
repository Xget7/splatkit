package com.splatkit.engine

import android.view.Surface
import com.splatkit.CameraPose
import com.splatkit.SplatStats

/**
 * The JNI boundary to the C++ engine: one opaque handle owned by native code, one
 * method per native entry point, and the two float array layouts the engine fills,
 * decoded here so that no other class knows them.
 *
 * Threads: every call runs on the render thread, except the loaders, which decode on
 * the calling thread, and the readers marked "any thread", which lock against [destroy].
 */
internal class NativeEngine {
    private var handle: Long = nativeCreate()
    private val poseScratch = FloatArray(POSE_FLOATS)
    private val statsScratch = FloatArray(STATS_FLOATS)

    /** False when Vulkan could not be brought up; every call is then a no-op. */
    val isValid: Boolean get() = handle != 0L

    /** The ordinals are `Engine::Event` on the C++ side. */
    enum class Event { WORLD_READY, WORLD_FAILED, COLLIDER_READY, COLLIDER_FAILED }

    /** Receives [Event]s on the thread that raised them: loader or render thread. */
    var onEvent: ((Event, String, Int) -> Unit)? = null

    // Called from JNI; the name and signature are part of the native contract.
    @Suppress("unused")
    private fun onNativeEvent(kind: Int, message: String, splatCount: Int) {
        val event = Event.entries.getOrNull(kind) ?: return
        onEvent?.invoke(event, message, splatCount)
    }

    // Surface and frames.

    fun setSurface(surface: Surface?) = nativeSetSurface(handle, surface)
    fun surfaceResized(width: Int, height: Int) = nativeSurfaceResized(handle, width, height)
    fun render(frameTimeNanos: Long) = nativeRender(handle, frameTimeNanos)

    // Loading: decodes on the calling thread.

    fun loadWorld(spzBytes: ByteArray) = nativeLoadWorld(handle, spzBytes)
    fun loadCollider(glbBytes: ByteArray) = nativeLoadCollider(handle, glbBytes)
    fun loadWorldFile(path: String) = nativeLoadWorldFile(handle, path)
    fun loadColliderFile(path: String) = nativeLoadColliderFile(handle, path)

    // Camera and input.

    fun setCameraPose(pose: CameraPose) =
        nativeSetCameraPose(handle, pose.x, pose.y, pose.z, pose.yaw, pose.pitch)

    /** The pose as of the last frame, or null once destroyed. Any thread. */
    fun cameraPose(): CameraPose? = synchronized(this) {
        if (handle == 0L) return null
        nativeCameraPose(handle, poseScratch)
        CameraPose(poseScratch[0], poseScratch[1], poseScratch[2], poseScratch[3], poseScratch[4])
    }

    fun look(deltaYaw: Float, deltaPitch: Float) = nativeLook(handle, deltaYaw, deltaPitch)
    fun walk(forward: Float, right: Float) = nativeWalk(handle, forward, right)
    fun setVelocity(forward: Float, right: Float) = nativeSetVelocity(handle, forward, right)
    fun setAttitude(rowMajor: FloatArray) = nativeSetAttitude(handle, rowMajor)
    fun setMotionEnabled(enabled: Boolean) = nativeSetMotionEnabled(handle, enabled)

    // Quality settings.

    fun setRenderScale(scale: Float) = nativeSetRenderScale(handle, scale)
    fun setCullMargin(degrees: Float) = nativeSetCullMargin(handle, degrees)
    fun setLinearBlending(linear: Boolean) = nativeSetLinearBlending(handle, linear)
    fun setSplatBudget(budget: Int) = nativeSetSplatBudget(handle, budget)
    fun setMaxShDegree(degree: Int) = nativeSetMaxShDegree(handle, degree)
    fun setShDegree(degree: Int) = nativeSetShDegree(handle, degree)

    // Diagnostics.

    fun startBenchmark(seconds: Float) = nativeStartBenchmark(handle, seconds)

    /** GPU name and Vulkan version, or an empty string once destroyed. Any thread. */
    fun gpuDescription(): String = synchronized(this) {
        if (handle != 0L) nativeGpuDescription(handle) else ""
    }

    /** Fills [into] with the latest stats; leaves it once destroyed. Any thread. */
    fun readStats(into: SplatStats): SplatStats = synchronized(this) {
        if (handle == 0L) return into
        nativeStats(handle, statsScratch)
        into.fps = statsScratch[0]
        into.frameMillis = statsScratch[1]
        into.gpuMillis = statsScratch[2]
        into.sortMillis = statsScratch[3]
        into.splatCount = statsScratch[4].toInt()
        into.walking = statsScratch[5] != 0f
        into.motion = statsScratch[6] != 0f
        into
    }

    // The any-thread readers above take the same lock, so none of them can run on a
    // handle that is being freed. Everything else runs on the render thread, like this.
    fun destroy(): Unit = synchronized(this) {
        if (handle != 0L) {
            nativeDestroy(handle)
            handle = 0L
        }
    }

    private external fun nativeCreate(): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeGpuDescription(handle: Long): String
    private external fun nativeSetSurface(handle: Long, surface: Surface?)
    private external fun nativeSurfaceResized(handle: Long, width: Int, height: Int)
    private external fun nativeRender(handle: Long, frameTimeNanos: Long)
    private external fun nativeLoadWorld(handle: Long, spzBytes: ByteArray)
    private external fun nativeLoadCollider(handle: Long, glbBytes: ByteArray)
    private external fun nativeLoadWorldFile(handle: Long, path: String)
    private external fun nativeLoadColliderFile(handle: Long, path: String)
    private external fun nativeSetCameraPose(handle: Long, x: Float, y: Float, z: Float, yaw: Float, pitch: Float)
    /** Fills [out] (at least [POSE_FLOATS]) with x, y, z, yaw, pitch. */
    private external fun nativeCameraPose(handle: Long, out: FloatArray)
    private external fun nativeLook(handle: Long, deltaYaw: Float, deltaPitch: Float)
    private external fun nativeWalk(handle: Long, forward: Float, right: Float)
    private external fun nativeSetVelocity(handle: Long, forward: Float, right: Float)
    private external fun nativeSetAttitude(handle: Long, rowMajor: FloatArray)
    private external fun nativeSetMotionEnabled(handle: Long, enabled: Boolean)
    private external fun nativeSetRenderScale(handle: Long, scale: Float)
    private external fun nativeSetCullMargin(handle: Long, degrees: Float)
    private external fun nativeSetLinearBlending(handle: Long, linear: Boolean)
    private external fun nativeSetSplatBudget(handle: Long, budget: Int)
    private external fun nativeSetMaxShDegree(handle: Long, degree: Int)
    private external fun nativeSetShDegree(handle: Long, degree: Int)
    private external fun nativeStartBenchmark(handle: Long, seconds: Float)
    /** Fills [out] (at least [STATS_FLOATS]) with fps, frame ms, gpu ms, sort ms, splats, walking, motion. */
    private external fun nativeStats(handle: Long, out: FloatArray)

    private companion object {
        const val POSE_FLOATS = 5
        const val STATS_FLOATS = 7

        init {
            System.loadLibrary("splatkit")
        }
    }
}
