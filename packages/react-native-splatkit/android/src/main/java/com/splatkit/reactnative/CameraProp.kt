package com.splatkit.reactnative

import com.facebook.react.bridge.ReadableMap
import com.splatkit.CameraMode
import com.splatkit.CameraRequest
import com.splatkit.WorldPoint

internal data class RevisionedCamera(val revision: Int, val request: CameraRequest)

internal fun cameraPropRevision(map: ReadableMap): Int =
    runCatching { map.int32("revision") }.getOrDefault(0)

/** Validates the complete wire request before narrowing Double values to Float. */
internal fun parseCameraProp(map: ReadableMap): RevisionedCamera? {
    val revision = map.int32("revision")
    if (revision == 0) return null
    require(revision > 0) { "revision must be positive" }
    val mode = CameraMode.fromWire(map.int32("mode"))
    require(mode != null) { "mode must be 0 or 1" }
    val anchor = WorldPoint(map.float32("anchorX"), map.float32("anchorY"),
        map.float32("anchorZ"))
    val radius = map.float32("radius")
    require(radius > 0f) { "radius must be positive" }
    return RevisionedCamera(revision, CameraRequest(mode, anchor, radius,
        map.float32("azimuth"), map.float32("elevation"),
        map.float32("orbitRadiansPerSecond")))
}
