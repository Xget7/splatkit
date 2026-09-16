package com.splatkit.reactnative

import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.widget.FrameLayout
import com.facebook.react.bridge.Arguments
import com.facebook.react.bridge.LifecycleEventListener
import com.facebook.react.bridge.WritableMap
import com.facebook.react.common.LifecycleState
import com.facebook.react.uimanager.ThemedReactContext
import com.facebook.react.uimanager.UIManagerHelper
import com.facebook.react.uimanager.events.Event
import com.splatkit.RenderPolicy
import com.splatkit.RenderPolicyResolution
import com.splatkit.SplatSurfaceView
import java.io.File

internal class SplatKitEvent(surfaceId: Int, tag: Int, private val name: String, private val data: WritableMap) :
    Event<SplatKitEvent>(surfaceId, tag) {
    override fun getEventName() = name
    override fun getEventData() = data
    override fun canCoalesce() = false
}

class SplatKitView(private val reactContext: ThemedReactContext) : FrameLayout(reactContext), LifecycleEventListener {
    private val main = Handler(Looper.getMainLooper())
    private val session = WorldSession()
    private var request: WorldRequest? = null
    // Props arrive one setter at a time; the transaction commits them together in commitProps.
    private var pendingRequest: WorldRequest? = null
    private var worldChanged = false
    private var nativeView: SplatSurfaceView? = null
    private var hostResumed = reactContext.lifecycleState == LifecycleState.RESUMED
    private var dropped = false
    private var paused = false
    private var renderScale = 1.0
    private var shDegree = 3
    private var displayChanged = false
    // The last valid host policy, applied to the current engine and every engine built after.
    private var policy: RevisionedPolicy? = null
    private var policyChanged = false

    init { reactContext.addLifecycleEventListener(this) }

    internal fun setWorld(value: WorldRequest?) {
        if (dropped) return
        if (value != null && value != request) {
            try {
                value.validate()
            } catch (error: IllegalArgumentException) {
                emitWorld(value.requestId, "failed", 0, "INVALID_REQUEST", error.message.orEmpty())
                return
            }
        }
        pendingRequest = value
        worldChanged = value != request
    }

    internal fun invalidRequest(message: String) = emitWorld("", "failed", 0, "INVALID_REQUEST", message)

    fun setPaused(value: Boolean) { paused = value; updateRunning() }
    fun setRenderScale(value: Double) {
        require(value.isFinite() && value in 0.1..2.0) { "renderScale must be in 0.1..2" }
        renderScale = value
        displayChanged = true
    }
    fun setShDegree(value: Int) {
        require(value in 0..3) { "shDegree must be in 0..3" }
        shDegree = value
        displayChanged = true
    }

    /**
     * Stores the host policy; [commitProps] applies it to the current engine, and every engine
     * a later world builds re-applies it. Null forgets it: the current engine keeps what it
     * has, and later engines start from the backend fallback.
     */
    internal fun setPolicy(value: RevisionedPolicy?) {
        if (dropped || value == policy) return
        policy = value
        policyChanged = value != null
    }

    /** A malformed policy prop: reported by revision, and the last valid policy stays. */
    internal fun invalidPolicy(revision: Int, message: String) {
        val current = nativeView?.renderPolicy ?: RenderPolicy()
        emitPolicy(revision, RenderPolicyResolution(current, accepted = false, error = message))
    }

    /**
     * Ends one prop transaction: a new world first, since its engine takes the display settings
     * and the policy too; an engine about to be replaced is not reconfigured.
     */
    internal fun commitProps() {
        if (dropped) return
        if (worldChanged) {
            worldChanged = false
            clearNative()
            request = pendingRequest
            if (isAttachedToWindow) createNative()
        }
        if (displayChanged) {
            displayChanged = false
            nativeView?.let {
                it.renderScale = renderScale.toFloat()
                it.shDegree = shDegree
            }
        }
        if (policyChanged) nativeView?.takeIf { it.isAvailable }?.let(::applyPolicy)
    }

    private fun applyPolicy(view: SplatSurfaceView) {
        policyChanged = false
        val requested = policy ?: return
        val token = session.generation
        view.applyRenderPolicy(requested.policy) { resolution ->
            // A replaced or released engine's outcome no longer describes this view.
            if (!dropped && token == session.generation) emitPolicy(requested.revision, resolution)
        }
    }

    private fun createNative() {
        val world = request ?: return
        if (dropped || nativeView != null) return
        val token = session.generation
        val view = SplatSurfaceView(reactContext)
        nativeView = view
        view.renderScale = renderScale.toFloat()
        view.shDegree = shDegree
        view.listener = object : SplatSurfaceView.Listener {
            private fun outcome(phase: String, count: Int = 0, message: String = "") {
                if (dropped || !session.accept(token, phase)) return
                emitWorld(world.requestId, phase, count, if (phase == "failed") "WORLD_LOAD_FAILED" else "", message)
            }
            override fun onWorldReady(splatCount: Int) = outcome("uploaded", splatCount)
            override fun onWorldFrameReady(splatCount: Int) = outcome("frameReady", splatCount)
            override fun onWorldFailed(message: String) = outcome("failed", message = message)
        }
        addView(view, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
        // Fabric lays this view out, but not native children added after mount: a later world's
        // view would stay 0x0, so Android would never create its surface.
        view.measure(MeasureSpec.makeMeasureSpec(width, MeasureSpec.EXACTLY), MeasureSpec.makeMeasureSpec(height, MeasureSpec.EXACTLY))
        view.layout(0, 0, width, height)
        if (!view.isAvailable) {
            if (session.accept(token, "failed")) emitWorld(world.requestId, "failed", 0, "GPU_UNAVAILABLE", "Vulkan initialization failed")
            return
        }
        emitCapabilities(view)
        // Queued on the render thread ahead of the load, so the decode observes the policy.
        applyPolicy(view)
        view.loadWorld(File(world.filePath), world.maxShDegree, world.lodCapacitySplats, world.residencyCapacitySplats)
        updateRunning()
    }

    private fun clearNative() {
        session.replace()
        main.removeCallbacks(statsTick)
        nativeView?.let {
            it.listener = null
            // Remove first so SurfaceHolder detaches before the render thread shuts down.
            removeView(it)
            it.release()
        }
        nativeView = null
    }

    private fun updateRunning() {
        main.removeCallbacks(statsTick)
        if (!dropped && isAttachedToWindow && hostResumed && !paused) {
            nativeView?.resume()
            if (nativeView != null) main.postDelayed(statsTick, 500)
        } else nativeView?.pause()
    }

    private val statsTick = object : Runnable {
        override fun run() {
            if (dropped || !isAttachedToWindow || !hostResumed || paused) return
            val view = nativeView ?: return
            val world = request ?: return
            if (session.sample(SystemClock.uptimeMillis())) {
                val stats = view.readStats()
                emit("topStats", Arguments.createMap().apply {
                    putString("requestId", world.requestId)
                    putDouble("loadedSplats", stats.loadedSplatCount.toDouble())
                    putDouble("drawnSplats", stats.drawnSplatCount.toDouble())
                    // The SDK does not expose timing validity bits. Do not infer availability from zero.
                    putDouble("frameMillis", 0.0); putBoolean("frameTimingAvailable", false)
                    putDouble("gpuMillis", 0.0); putBoolean("gpuTimingAvailable", false)
                    putDouble("sortMillis", 0.0); putBoolean("sortTimingAvailable", false)
                })
            }
            main.postDelayed(this, 500)
        }
    }

    private fun emitWorld(id: String, phase: String, count: Int, code: String, message: String) {
        emit("topWorldEvent", Arguments.createMap().apply {
            putString("requestId", id); putString("phase", phase)
            putDouble("loadedSplats", count.toDouble()); putString("errorCode", code); putString("message", message)
        })
    }

    /** Native limits, features and accepted policy; emitted once per engine. */
    private fun emitCapabilities(view: SplatSurfaceView) {
        val caps = view.deviceCapabilities ?: return
        emit("topCapabilities", Arguments.createMap().apply {
            putInt("maxLodCapacitySplats", caps.limits.maxLodCapacitySplats)
            putInt("minResidencyCapacitySplats", caps.limits.minResidencyCapacitySplats)
            putInt("maxResidencyCapacitySplats", caps.limits.maxResidencyCapacitySplats)
            putBoolean("supportsComputeTiles", caps.supportsComputeTiles)
            putBoolean("supportsHiZOcclusion", caps.supportsHiZOcclusion)
            putBoolean("supportsSubgroups", caps.supportsSubgroups)
            putInt("maxTextureDimension", caps.maxTextureDimension)
            putBoolean("policyRaster", caps.policy.raster)
            putBoolean("policyTileSize", caps.policy.tileSize)
            putBoolean("policyLodErrorPixels", caps.policy.lodErrorPixels)
            putBoolean("policyAlphaThreshold", caps.policy.alphaThreshold)
            putBoolean("policySubpixelThreshold", caps.policy.subpixelThreshold)
            putBoolean("policyEnableFrustumCulling", caps.policy.enableFrustumCulling)
            putBoolean("policyEnableHiZOcclusion", caps.policy.enableHiZOcclusion)
            putBoolean("policyEnableEarlyTermination", caps.policy.enableEarlyTermination)
            putBoolean("policySortDepth", caps.policy.sortDepth)
        })
    }

    /** The outcome and the policy in effect afterwards, tagged with the revision it answers. */
    private fun emitPolicy(revision: Int, resolution: RenderPolicyResolution) {
        val outcome = policyOutcome(resolution)
        val effective = resolution.effective
        emit("topPolicyEvent", Arguments.createMap().apply {
            putInt("revision", revision)
            putString("phase", outcome.phase)
            putString("errorCode", outcome.errorCode)
            putString("message", outcome.message)
            putInt("raster", effective.raster.wire)
            putInt("tileSize", effective.tileSize)
            putDouble("lodErrorPixels", effective.lodErrorPixels.toDouble())
            putDouble("alphaThreshold", effective.alphaThreshold.toDouble())
            putDouble("subpixelThreshold", effective.subpixelThreshold.toDouble())
            putBoolean("enableFrustumCulling", effective.enableFrustumCulling)
            putBoolean("enableHiZOcclusion", effective.enableHiZOcclusion)
            putBoolean("enableEarlyTermination", effective.enableEarlyTermination)
            putInt("sortDepth", effective.sortDepth.wire)
        })
    }

    private fun emit(name: String, payload: WritableMap) {
        if (dropped) return
        UIManagerHelper.getEventDispatcher(reactContext)?.dispatchEvent(
            SplatKitEvent(UIManagerHelper.getSurfaceId(this), id, name, payload))
    }

    override fun onAttachedToWindow() { super.onAttachedToWindow(); createNative(); updateRunning() }
    override fun onDetachedFromWindow() { nativeView?.pause(); main.removeCallbacks(statsTick); super.onDetachedFromWindow() }
    override fun onHostResume() { hostResumed = true; updateRunning() }
    override fun onHostPause() { hostResumed = false; updateRunning() }
    override fun onHostDestroy() { dispose() }

    fun dispose() {
        if (dropped) return
        dropped = true
        reactContext.removeLifecycleEventListener(this)
        clearNative()
        request = null
        pendingRequest = null
        policy = null
    }
}
