package com.splatkit

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class RenderPolicyTest {
    @Test
    fun roundTripsEveryPolicyField() {
        val policy = RenderPolicy(
            raster = RasterStrategy.HYBRID,
            tileSize = 32,
            lodErrorPixels = 1.25f,
            alphaThreshold = 0.5f,
            subpixelThreshold = 2.5f,
            enableFrustumCulling = false,
            enableHiZOcclusion = true,
            enableEarlyTermination = false,
            sortDepth = SortDepth.BITS_16,
        )
        val encoded = DoubleArray(RENDER_POLICY_FIELDS)
        encodeRenderPolicy(policy, encoded)
        assertEquals(policy, decodeRenderPolicy(encoded))
    }

    @Test
    fun validationMirrorsTheJsRanges() {
        assertNull(renderPolicyError(RenderPolicy()))
        assertEquals("tileSize must be 8, 16 or 32", renderPolicyError(RenderPolicy(tileSize = 12)))
        assertTrue(renderPolicyError(RenderPolicy(lodErrorPixels = 0f)) != null)
        assertTrue(renderPolicyError(RenderPolicy(lodErrorPixels = Float.NaN)) != null)
        assertTrue(renderPolicyError(RenderPolicy(alphaThreshold = 1.5f)) != null)
        assertTrue(renderPolicyError(RenderPolicy(alphaThreshold = -0.1f)) != null)
        assertTrue(renderPolicyError(RenderPolicy(subpixelThreshold = -1f)) != null)
        assertTrue(renderPolicyError(RenderPolicy(subpixelThreshold = Float.POSITIVE_INFINITY)) != null)
    }

    @Test
    fun decodesAnAcceptedResolutionWithItsWarnings() {
        val values = DoubleArray(RENDER_POLICY_RESOLUTION_FIELDS)
        values[0] = 1.0
        val effective = RenderPolicy(subpixelThreshold = 2f, sortDepth = SortDepth.BITS_16)
        encodeRenderPolicy(effective, values, offset = 2)

        val resolution = decodeRenderPolicyResolution(values, arrayOf("raster fell back", "tiles fell back"))
        assertTrue(resolution.accepted)
        assertFalse(resolution.preparationFailed)
        assertNull(resolution.error)
        assertEquals(effective, resolution.effective)
        assertEquals(listOf("raster fell back", "tiles fell back"), resolution.warnings)
    }

    @Test
    fun decodesARejectionAsItsReasonAndThePolicyStillInEffect() {
        val values = DoubleArray(RENDER_POLICY_RESOLUTION_FIELDS)
        values[1] = 1.0
        val previous = RenderPolicy(sortDepth = SortDepth.BITS_16)
        encodeRenderPolicy(previous, values, offset = 2)

        val resolution = decodeRenderPolicyResolution(values, arrayOf("pipeline rebuild failed"))
        assertFalse(resolution.accepted)
        assertTrue(resolution.preparationFailed)
        assertEquals("pipeline rebuild failed", resolution.error)
        assertEquals(previous, resolution.effective)
        assertTrue(resolution.warnings.isEmpty())
    }

    @Test
    fun decodesTheCapabilityLayout() {
        val values = DoubleArray(CAPABILITY_FIELDS)
        values[0] = 2_200_000.0
        values[1] = 100_000.0
        values[2] = 8_000_000.0
        values[3] = 1.0
        values[5] = 1.0
        values[6] = 16_384.0
        values[15] = 1.0          // policy.sortDepth supported
        values[11] = 1.0          // policy.subpixelThreshold supported
        values[21] = 0.0          // fallback raster hardware
        values[22] = 16.0         // fallback tileSize
        values[26] = 1.0          // fallback frustum culling
        values[29] = 32.0         // fallback sortDepth

        val capabilities = decodeDeviceCapabilities(values)
        assertEquals(2_200_000, capabilities.limits.maxLodCapacitySplats)
        assertEquals(8_000_000, capabilities.limits.maxResidencyCapacitySplats)
        assertTrue(capabilities.supportsComputeTiles)
        assertFalse(capabilities.supportsHiZOcclusion)
        assertTrue(capabilities.supportsSubgroups)
        assertEquals(16_384, capabilities.maxTextureDimension)
        assertTrue(capabilities.policy.sortDepth)
        assertTrue(capabilities.policy.subpixelThreshold)
        assertEquals(RasterStrategy.HARDWARE, capabilities.fallback.raster)
        assertEquals(16, capabilities.fallback.tileSize)
        assertEquals(SortDepth.BITS_32, capabilities.fallback.sortDepth)
    }
}
