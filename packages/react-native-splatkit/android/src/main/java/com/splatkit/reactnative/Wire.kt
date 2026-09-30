package com.splatkit.reactnative

import com.facebook.react.bridge.ReadableMap
import com.facebook.react.bridge.ReadableType

// The spellings below are the ones the JS contracts, the codegen spec and the iOS adapter use.

internal object WorldPhase {
    const val UPLOADED = "uploaded"
    const val FRAME_READY = "frameReady"
    const val FAILED = "failed"
}

internal object ColliderPhase {
    const val READY = "ready"
    const val FAILED = "failed"
}

internal object CameraPhase {
    const val APPLIED = "applied"
    const val REJECTED = "rejected"
}

internal object PolicyPhase {
    const val APPLIED = "applied"
    const val WARNING = "warning"
    const val REJECTED = "rejected"
}

internal object ErrorCode {
    const val INVALID_REQUEST = "INVALID_REQUEST"
    const val GPU_UNAVAILABLE = "GPU_UNAVAILABLE"
    const val WORLD_LOAD_FAILED = "WORLD_LOAD_FAILED"
    const val COLLIDER_LOAD_FAILED = "COLLIDER_LOAD_FAILED"
    const val INVALID_CAMERA = "INVALID_CAMERA"
    const val INVALID_POLICY = "INVALID_POLICY"
    const val POLICY_PREPARATION_FAILED = "POLICY_PREPARATION_FAILED"
}

/** Direct event types, each registered under the `on...` prop it reaches in SplatKitViewManager. */
internal object EventType {
    const val WORLD = "topWorldEvent"
    const val STATS = "topStats"
    const val POLICY = "topPolicyEvent"
    const val CAMERA = "topCameraEvent"
    const val CAPABILITIES = "topCapabilities"
    const val COLLIDER = "topColliderEvent"
    const val CAMERA_POSE = "topCameraPose"
    const val FOCUS_RESULT = "topFocusResult"
}

internal fun ReadableMap.number(name: String): Double {
    require(hasKey(name) && getType(name) == ReadableType.Number) { "$name must be a number" }
    return getDouble(name)
}

/** A number that is exactly an Int32: codegen transports Int32 props as doubles. */
internal fun ReadableMap.int32(name: String): Int {
    val value = number(name)
    require(value.isFinite() && value == value.toInt().toDouble()) { "$name must be an Int32" }
    return value.toInt()
}

internal fun ReadableMap.float32(name: String): Float {
    val value = number(name)
    require(value.isFinite() && value.toFloat().isFinite()) { "$name must be a finite Float32" }
    return value.toFloat()
}

internal fun ReadableMap.flag(name: String): Boolean {
    require(hasKey(name) && getType(name) == ReadableType.Boolean) { "$name must be a boolean" }
    return getBoolean(name)
}
