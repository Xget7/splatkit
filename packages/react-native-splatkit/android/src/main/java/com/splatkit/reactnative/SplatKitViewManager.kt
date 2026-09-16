package com.splatkit.reactnative

import com.facebook.react.bridge.ReadableMap
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
    )
    override fun onDropViewInstance(view: SplatKitView) { view.dispose(); super.onDropViewInstance(view) }
}
