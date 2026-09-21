package com.splatkit

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.AttributeSet
import android.util.Log
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import com.splatkit.engine.SplatEngine
import com.splatkit.engine.RenderThread
import com.splatkit.input.MotionInput
import com.splatkit.input.TouchInput
import java.io.File

private const val TAG = "SplatKit"

internal fun dispatchSplatEvent(
    listener: SplatSurfaceView.Listener,
    event: SplatEngine.Event,
    message: String,
    splatCount: Int,
) {
    when (event) {
        SplatEngine.Event.WORLD_READY -> listener.onWorldReady(splatCount)
        SplatEngine.Event.WORLD_FRAME_READY -> listener.onWorldFrameReady(splatCount)
        SplatEngine.Event.WORLD_FAILED -> listener.onWorldFailed(message)
        SplatEngine.Event.COLLIDER_READY -> listener.onColliderReady()
        SplatEngine.Event.COLLIDER_FAILED -> listener.onColliderFailed(message)
    }
}

/**
 * A SurfaceView that renders with SplatKit.
 *
 * The host activity forwards resume, pause and release. Everything else follows the
 * surface lifecycle: the engine gets the surface when Android creates it and gives it
 * back, synchronously, before Android destroys it.
 *
 * Gestures: a finger drags the view to look (yaw, and pitch when the gyroscope is off),
 * and that can be turned off. The view ships no walking control: the host draws its own,
 * wherever it likes, and drives [setWalkVelocity] or [walk] from it.
 * [com.splatkit.ui.JoystickView] is one such control, for a host that wants a ready-made one.
 */
class SplatSurfaceView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : SurfaceView(context, attrs), SurfaceHolder.Callback {

    private val renderThread = RenderThread()
    private val motion = MotionInput(context, renderThread.renderHandler) { renderThread.setAttitude(it) }
    private val touch = TouchInput(object : TouchInput.Listener {
        override fun onLook(deltaYaw: Float, deltaPitch: Float) = renderThread.look(deltaYaw, deltaPitch)
    })
    private var motionEnabled = false
    private var resumed = false
    private val poseHandler = Handler(Looper.getMainLooper())
    private var lastPose: CameraPose? = null
    private val reportPose = object : Runnable {
        override fun run() {
            val pose = cameraPose
            if (pose != lastPose) {
                lastPose = pose
                cameraPoseListener?.invoke(pose)
            }
            poseHandler.postDelayed(this, cameraPoseIntervalMillis)
        }
    }

    /** Radians per pixel dragged. */
    var lookSensitivity: Float
        get() = touch.lookSensitivity
        set(value) { touch.lookSensitivity = value }

    /**
     * Whether a drag on the view is allowed to turn the camera. A host that drives looking
     * from its own control, and scripted tours, turn it off.
     */
    var touchLookEnabled: Boolean
        get() = touch.lookEnabled
        set(value) {
            touch.lookEnabled = value
            if (!value) touch.letGo()
        }

    /**
     * Where the camera is, on the main thread, at most this often in milliseconds, and only
     * when it differs from the pose last delivered. Zero, the default, never reports.
     */
    var cameraPoseIntervalMillis: Long = 0
        set(value) {
            field = value.coerceAtLeast(0)
            startPoseReports()
        }

    /** Called with each pose [cameraPoseIntervalMillis] asks for. */
    var cameraPoseListener: ((CameraPose) -> Unit)? = null
        set(value) {
            field = value
            startPoseReports()
        }

    init {
        holder.addCallback(this)
        renderThread.onEvent = { event, message, splatCount ->
            listener?.let { dispatchSplatEvent(it, event, message, splatCount) }
        }
    }

    /** Loading and first-frame outcomes on the main thread. All methods have empty defaults. */
    interface Listener {
        /** The world is uploaded; its first GPU frame may still be pending. */
        fun onWorldReady(splatCount: Int) {}
        /** The GPU completed a frame drawing this world. Does not guarantee display scanout. */
        fun onWorldFrameReady(splatCount: Int) {}
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

    /** Applies load-time options on the render thread before scheduling this file's decode. */
    fun loadWorld(file: File, maxShDegree: Int, splatBudget: Int, residencyBudget: Int) {
        require(maxShDegree in 0..3 && splatBudget >= 0 && residencyBudget in 100_000..8_000_000)
        renderThread.loadWorldFile(file.absolutePath, maxShDegree, splatBudget, residencyBudget)
    }

    /** Decodes a collider GLB from a file; enables walk mode when ready. */
    fun loadCollider(file: File) = renderThread.loadColliderFile(file.absolutePath)

    /**
     * Shows a tiled world from its index, a `tileset.json` with its tiles beside it (made
     * offline by `splat-tile`). Only the index is read now; tiles stream in as the camera
     * needs them, nearest and biggest on screen first, within [residencyBudget].
     */
    fun loadTiledWorld(tileset: File) = renderThread.loadTiledWorldFile(tileset.absolutePath)

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
     * CPU fallback's angular culling margin, in degrees. The GPU path evaluates the
     * current camera each frame and uses projected splat bounds instead.
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
     * LOD selection capacity for subsequently loaded worlds. Zero disables automatic
     * hierarchy construction; a .lodsplat file already contains its hierarchy.
     * Vulkan GPU selection supports at most 2.2M nodes. Coarse parents are approximate,
     * and the full hierarchy must fit GPU memory. Non-LOD visibility above 3M survivors
     * fails closed instead of truncating or issuing an unsafe draw.
     */
    var splatBudget: Int = 0
        set(value) {
            field = value.coerceAtLeast(0)
            renderThread.setSplatBudget(field)
        }

    /**
     * Residency budget of a tiled world: the most splats held on the GPU at once, about
     * 32 bytes each plus the harmonics. Streaming fills it with what is nearest and
     * biggest on screen and evicts what the camera left. Applies to tiled worlds loaded
     * after it is set.
     */
    var residencyBudget: Int = 2_000_000
        set(value) {
            field = value.coerceIn(100_000, 8_000_000)
            renderThread.setResidencyBudget(field)
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

    /**
     * Walks continuously at the given speed in meters per second until called again with
     * zeros: what a joystick or a keyboard drives. Forward is where the camera looks,
     * flattened onto the floor while walking; right strafes.
     */
    fun setWalkVelocity(forward: Float, right: Float) = renderThread.setVelocity(forward, right)

    /**
     * One step, in meters, for a host that integrates movement itself. The collider stops
     * it at walls and the floor carries it, exactly as a velocity would.
     */
    fun walk(forward: Float, right: Float) = renderThread.walk(forward, right)

    /**
     * Turns the camera by these radians: what a look pad or a mouse drives. Pitch is
     * clamped, and ignored while the gyroscope drives the view.
     */
    fun look(deltaYaw: Float, deltaPitch: Float) = renderThread.look(deltaYaw, deltaPitch)

    /**
     * The walker's shape in walk mode, applied at once and to a collider loaded later.
     * False when a value is not a walkable one, and then the previous settings stay.
     */
    fun setCharacter(settings: CharacterSettings): Boolean {
        val accepted = renderThread.setCharacter(settings)
        if (accepted) character = settings
        return accepted
    }

    /** The walker's shape in effect. */
    var character = CharacterSettings()
        private set

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

    /**
     * Applies a render policy to this view's engine on the render thread, before any world
     * loaded afterwards. The engine re-validates the whole request: an invalid one, or one the
     * GPU cannot prepare, keeps the previous policy; unsupported valid choices fall back with a
     * warning each. [completion] receives the outcome on the main thread, in request order.
     */
    fun applyRenderPolicy(policy: RenderPolicy, completion: ((RenderPolicyResolution) -> Unit)? = null) =
        renderThread.applyRenderPolicy(policy, completion)

    /** The policy currently applied, or null before the engine exists. Any thread. */
    val renderPolicy: RenderPolicy? get() = renderThread.renderPolicy()

    /** Native limits, features and accepted policy, or null before the engine exists. */
    val deviceCapabilities: DeviceCapabilities? get() = renderThread.deviceCapabilities()

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
        startPoseReports()
    }

    fun pause() {
        resumed = false
        motion.stop()
        renderThread.pause()
        poseHandler.removeCallbacks(reportPose)
    }

    fun release() {
        motion.stop()
        poseHandler.removeCallbacks(reportPose)
        holder.removeCallback(this)
        renderThread.release()
    }

    private fun startPoseReports() {
        poseHandler.removeCallbacks(reportPose)
        if (resumed && cameraPoseIntervalMillis > 0 && cameraPoseListener != null) {
            poseHandler.postDelayed(reportPose, cameraPoseIntervalMillis)
        }
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
