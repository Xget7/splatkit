package com.splatkit.reactnative

import com.facebook.react.bridge.ReadableMap
import com.facebook.react.bridge.ReadableType
import com.splatkit.RasterStrategy
import com.splatkit.RenderPolicy
import com.splatkit.RenderPolicyResolution
import com.splatkit.SortDepth

/** The host policy and the revision native reports it under. */
internal data class RevisionedPolicy(val revision: Int, val policy: RenderPolicy)

/** What `onPolicyEvent` says about one application. */
internal data class PolicyOutcome(val phase: String, val errorCode: String, val message: String)

private fun ReadableMap.number(name: String): Double {
    require(hasKey(name) && getType(name) == ReadableType.Number) { "$name must be a number" }
    return getDouble(name)
}

private fun ReadableMap.int32(name: String): Int {
    val value = number(name)
    require(value.isFinite() && value == value.toInt().toDouble()) { "$name must be an Int32" }
    return value.toInt()
}

private fun ReadableMap.flag(name: String): Boolean {
    require(hasKey(name) && getType(name) == ReadableType.Boolean) { "$name must be a boolean" }
    return getBoolean(name)
}

/** The revision of a policy prop, or 0 when even that is malformed. */
internal fun policyPropRevision(map: ReadableMap): Int = runCatching { map.int32("revision") }.getOrDefault(0)

/**
 * Parses the Fabric `policy` prop. Enum wire values must be known: an unknown raster or sort
 * depth is invalid, never a silent default. Ranges are re-validated by the engine.
 * Returns null for a revision of 0 or less, which both adapters treat as no policy.
 */
internal fun parsePolicyProp(map: ReadableMap): RevisionedPolicy? {
    val revision = map.int32("revision")
    if (revision <= 0) return null
    val raster = map.int32("raster")
    val sortDepth = map.int32("sortDepth")
    return RevisionedPolicy(revision, RenderPolicy(
        raster = requireNotNull(RasterStrategy.fromWire(raster)) { "raster must be 0, 1 or 2" },
        tileSize = map.int32("tileSize"),
        lodErrorPixels = map.number("lodErrorPixels").toFloat(),
        alphaThreshold = map.number("alphaThreshold").toFloat(),
        subpixelThreshold = map.number("subpixelThreshold").toFloat(),
        enableFrustumCulling = map.flag("enableFrustumCulling"),
        enableHiZOcclusion = map.flag("enableHiZOcclusion"),
        enableEarlyTermination = map.flag("enableEarlyTermination"),
        sortDepth = requireNotNull(SortDepth.fromWire(sortDepth)) { "sortDepth must be 16 or 32" },
    ))
}

/** Shared with the iOS adapter: the codes a host can branch on. */
internal fun policyOutcome(resolution: RenderPolicyResolution): PolicyOutcome = when {
    !resolution.accepted -> PolicyOutcome("rejected",
        if (resolution.preparationFailed) "POLICY_PREPARATION_FAILED" else "INVALID_POLICY",
        resolution.error.orEmpty())
    resolution.warnings.isNotEmpty() -> PolicyOutcome("warning", "", resolution.warnings.joinToString("; "))
    else -> PolicyOutcome("applied", "", "")
}
