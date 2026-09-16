package com.splatkit.reactnative

import com.facebook.react.bridge.JavaOnlyMap
import com.splatkit.RasterStrategy
import com.splatkit.RenderPolicy
import com.splatkit.RenderPolicyResolution
import com.splatkit.SortDepth
import org.junit.Assert.*
import org.junit.Test

class PolicyPropTest {
    private fun prop(vararg overrides: Pair<String, Any>): JavaOnlyMap {
        val values = linkedMapOf<String, Any>(
            "revision" to 3.0, "raster" to 2.0, "tileSize" to 16.0, "lodErrorPixels" to 1.25,
            "alphaThreshold" to 1.0 / 255.0, "subpixelThreshold" to 1.0,
            "enableFrustumCulling" to true, "enableHiZOcclusion" to false,
            "enableEarlyTermination" to true, "sortDepth" to 16.0,
        )
        values.putAll(overrides)
        return JavaOnlyMap.of(*values.flatMap { listOf(it.key, it.value) }.toTypedArray())
    }

    @Test fun parsesEveryFieldWithItsRevision() {
        val parsed = parsePolicyProp(prop())!!
        assertEquals(3, parsed.revision)
        assertEquals(RasterStrategy.HYBRID, parsed.policy.raster)
        assertEquals(16, parsed.policy.tileSize)
        assertEquals(1.25f, parsed.policy.lodErrorPixels)
        assertEquals(1f, parsed.policy.subpixelThreshold)
        assertEquals(SortDepth.BITS_16, parsed.policy.sortDepth)
        assertTrue(parsed.policy.enableFrustumCulling)
        assertFalse(parsed.policy.enableHiZOcclusion)
    }

    @Test fun unknownEnumWireValuesAreInvalidNotDefaulted() {
        assertThrows(IllegalArgumentException::class.java) { parsePolicyProp(prop("raster" to 7.0)) }
        assertThrows(IllegalArgumentException::class.java) { parsePolicyProp(prop("sortDepth" to 24.0)) }
    }

    @Test fun missingOrMistypedFieldsAreInvalid() {
        val missing = prop().apply { remove("tileSize") }
        assertThrows(IllegalArgumentException::class.java) { parsePolicyProp(missing) }
        assertThrows(IllegalArgumentException::class.java) { parsePolicyProp(prop("tileSize" to 16.5)) }
        assertThrows(IllegalArgumentException::class.java) { parsePolicyProp(prop("enableHiZOcclusion" to 1.0)) }
        // The revision survives a malformed body so the rejection can be reported under it.
        assertEquals(3, policyPropRevision(missing))
        assertEquals(0, policyPropRevision(prop("revision" to "three")))
    }

    @Test fun revisionZeroOrLessMeansNoPolicy() {
        assertNull(parsePolicyProp(prop("revision" to 0.0)))
        assertNull(parsePolicyProp(prop("revision" to -2.0)))
    }

    @Test fun outcomesUseTheCodesSharedWithIos() {
        val policy = RenderPolicy()
        assertEquals(PolicyOutcome("applied", "", ""), policyOutcome(RenderPolicyResolution(policy)))
        assertEquals(PolicyOutcome("warning", "", "raster fell back; tiles fell back"),
            policyOutcome(RenderPolicyResolution(policy, warnings = listOf("raster fell back", "tiles fell back"))))
        assertEquals(PolicyOutcome("rejected", "INVALID_POLICY", "tileSize must be 8, 16 or 32"),
            policyOutcome(RenderPolicyResolution(policy, accepted = false, error = "tileSize must be 8, 16 or 32")))
        assertEquals(PolicyOutcome("rejected", "POLICY_PREPARATION_FAILED", "pipeline rebuild failed"),
            policyOutcome(RenderPolicyResolution(policy, accepted = false, preparationFailed = true,
                error = "pipeline rebuild failed")))
    }
}
