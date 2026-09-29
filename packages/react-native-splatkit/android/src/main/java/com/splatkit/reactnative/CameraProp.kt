package com.splatkit.reactnative

import com.facebook.react.bridge.ReadableMap
import com.facebook.react.bridge.ReadableType
import com.splatkit.CameraMode
import com.splatkit.CameraRequest
import com.splatkit.WorldPoint

internal data class RevisionedCamera(val revision: Int, val request: CameraRequest)

private fun ReadableMap.cameraNumber(name: String): Double {
    require(hasKey(name) && getType(name) == ReadableType.Number) { "$name must be a number" }
    return getDouble(name)
}

private fun ReadableMap.cameraInt32(name: String): Int {
    val value = cameraNumber(name)
    require(value.isFinite() && value >= Int.MIN_VALUE.toDouble() &&
        value <= Int.MAX_VALUE.toDouble() && value == value.toInt().toDouble()) {
        "$name must be an Int32"
    }
    return value.toInt()
}

private fun ReadableMap.cameraFloat(name: String): Float {
    val value = cameraNumber(name)
    require(value.isFinite() && value.toFloat().isFinite()) { "$name must be a finite Float32" }
    return value.toFloat()
}

internal fun cameraPropRevision(map: ReadableMap): Int =
    runCatching { map.cameraInt32("revision") }.getOrDefault(0)

/** Validates the complete wire request before narrowing Double values to Float. */
internal fun parseCameraProp(map: ReadableMap): RevisionedCamera? {
    val revision = map.cameraInt32("revision")
    if (revision == 0) return null
    require(revision > 0) { "revision must be positive" }
    val mode = CameraMode.fromWire(map.cameraInt32("mode"))
    require(mode != null) { "mode must be 0 or 1" }
    val anchor = WorldPoint(map.cameraFloat("anchorX"), map.cameraFloat("anchorY"),
        map.cameraFloat("anchorZ"))
    val radius = map.cameraFloat("radius")
    require(radius > 0f) { "radius must be positive" }
    return RevisionedCamera(revision, CameraRequest(mode, anchor, radius,
        map.cameraFloat("azimuth"), map.cameraFloat("elevation"),
        map.cameraFloat("orbitRadiansPerSecond")))
}
