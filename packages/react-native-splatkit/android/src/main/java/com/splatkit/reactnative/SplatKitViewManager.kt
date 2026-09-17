package com.splatkit.reactnative

import com.facebook.react.bridge.ReadableMap
import com.splatkit.CharacterSettings
import com.facebook.react.uimanager.SimpleViewManager
import com.facebook.react.uimanager.ThemedReactContext
import com.facebook.react.uimanager.ViewManagerDelegate
import com.facebook.react.viewmanagers.SplatKitViewManagerDelegate
import com.facebook.react.viewmanagers.SplatKitViewManagerInterface

class SplatKitViewManager : SimpleViewManager<SplatKitView>(), SplatKitViewManagerInterface<SplatKitView> {
    private fun ReadableMap.integer(name: String): Int {
        val value = getDouble(name)
        require(value.isFinite() && value == value.toInt().toDouble()) { "$name must be an Int32" }
        return value.toInt()
    }
    private val generatedDelegate = SplatKitViewManagerDelegate(this)
    override fun getDelegate(): ViewManagerDelegate<SplatKitView> = generatedDelegate
    override fun getName() = "SplatKitView"
    override fun createViewInstance(context: ThemedReactContext) = SplatKitView(context)
    override fun setPaused(view: SplatKitView, value: Boolean) = view.setPaused(value)
    override fun setRenderScale(view: SplatKitView, value: Double) = view.setRenderScale(value)
    override fun setShDegree(view: SplatKitView, value: Int) = view.setShDegree(value)
    override fun setWorld(view: SplatKitView, value: ReadableMap?) {
        if (value == null) { view.setWorld(null); return }
        try {
            view.setWorld(WorldRequest(
                value.getString("requestId") ?: "", value.getString("filePath") ?: "",
                value.integer("maxShDegree"), value.integer("lodCapacitySplats"), value.integer("residencyCapacitySplats")))
        } catch (error: RuntimeException) {
            view.invalidRequest(error.message ?: "Malformed world request")
        }
    }
    override fun setLinearBlending(view: SplatKitView, value: Boolean) = view.setLinearBlending(value)
    override fun setCullMarginDegrees(view: SplatKitView, value: Double) = view.setCullMarginDegrees(value)
    override fun setMotionEnabled(view: SplatKitView, value: Boolean) = view.setMotionEnabled(value)
    override fun setTouchLookEnabled(view: SplatKitView, value: Boolean) = view.setTouchLookEnabled(value)
    override fun setLookSensitivity(view: SplatKitView, value: Double) = view.setLookSensitivity(value)
    override fun setCameraPoseInterval(view: SplatKitView, value: Double) = view.setCameraPoseInterval(value)
    override fun setCollider(view: SplatKitView, value: ReadableMap?) {
        if (value == null) { view.setCollider(null); return }
        try {
            view.setCollider(ColliderRequest(
                value.getString("requestId") ?: "", value.getString("filePath") ?: ""))
        } catch (error: RuntimeException) {
            view.invalidCollider(error.message ?: "Malformed collider request")
        }
    }
    override fun setCharacter(view: SplatKitView, value: ReadableMap?) {
        if (value == null) { view.setCharacter(null); return }
        try {
            view.setCharacter(CharacterSettings(
                value.getDouble("eyeHeight").toFloat(), value.getDouble("bodyRadius").toFloat(),
                value.getDouble("stepHeight").toFloat()))
        } catch (error: RuntimeException) {
            view.setCharacter(null)
        }
    }
    override fun setWalkVelocity(view: SplatKitView, forward: Double, right: Double) =
        view.walk(forward, right)
    override fun look(view: SplatKitView, deltaYaw: Double, deltaPitch: Double) =
        view.look(deltaYaw, deltaPitch)
    override fun setCameraPose(
        view: SplatKitView, x: Double, y: Double, z: Double, yaw: Double, pitch: Double,
    ) = view.teleport(x, y, z, yaw, pitch)
    override fun setPolicy(view: SplatKitView, value: ReadableMap?) {
        if (value == null) { view.setPolicy(null); return }
        try {
            view.setPolicy(parsePolicyProp(value))
        } catch (error: RuntimeException) {
            view.invalidPolicy(policyPropRevision(value), error.message ?: "Malformed policy")
        }
    }
    // Fabric sets each changed prop, then ends the transaction here: a world and a policy
    // changed together are applied once, to the engine the new world builds.
    override fun onAfterUpdateTransaction(view: SplatKitView) {
        super.onAfterUpdateTransaction(view)
        view.commitProps()
    }
    override fun getExportedCustomDirectEventTypeConstants(): MutableMap<String, Any> = mutableMapOf(
        "topWorldEvent" to mapOf("registrationName" to "onWorldEvent"),
        "topStats" to mapOf("registrationName" to "onStats"),
        "topPolicyEvent" to mapOf("registrationName" to "onPolicyEvent"),
        "topCapabilities" to mapOf("registrationName" to "onCapabilities"),
        "topColliderEvent" to mapOf("registrationName" to "onColliderEvent"),
        "topCameraPose" to mapOf("registrationName" to "onCameraPose"),
    )
    override fun onDropViewInstance(view: SplatKitView) { view.dispose(); super.onDropViewInstance(view) }
}
