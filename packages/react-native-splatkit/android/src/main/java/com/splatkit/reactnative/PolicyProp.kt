package com.splatkit.reactnative

import com.facebook.react.bridge.ReadableMap
import com.splatkit.RasterStrategy
import com.splatkit.RenderPolicy
import com.splatkit.RenderPolicyResolution
import com.splatkit.SortDepth

/** The host policy and the revision native reports it under. */
internal data class RevisionedPolicy(val revision: Int, val policy: RenderPolicy)

/** What `onPolicyEvent` says about one application. */
internal data class PolicyOutcome(val phase: String, val errorCode: String, val message: String)

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
    !resolution.accepted -> PolicyOutcome(PolicyPhase.REJECTED,
        if (resolution.preparationFailed) ErrorCode.POLICY_PREPARATION_FAILED else ErrorCode.INVALID_POLICY,
        resolution.error.orEmpty())
    resolution.warnings.isNotEmpty() ->
        PolicyOutcome(PolicyPhase.WARNING, "", resolution.warnings.joinToString("; "))
    else -> PolicyOutcome(PolicyPhase.APPLIED, "", "")
}
