package com.splatkit.reactnative

import com.facebook.react.bridge.JavaOnlyMap
import com.splatkit.CameraMode
import org.junit.Assert.*
import org.junit.Test

class CameraPropTest {
    private fun prop(vararg overrides: Pair<String, Any>): JavaOnlyMap {
        val values = linkedMapOf<String, Any>(
            "revision" to 7.0, "mode" to 1.0,
            "anchorX" to 1.0, "anchorY" to 2.0, "anchorZ" to 3.0,
            "radius" to 4.0, "azimuth" to 0.5, "elevation" to -0.25,
            "orbitRadiansPerSecond" to 0.125,
        )
        values.putAll(overrides)
        return JavaOnlyMap.of(*values.flatMap { listOf(it.key, it.value) }.toTypedArray())
    }

    @Test fun parsesCompleteTransaction() {
        val parsed = parseCameraProp(prop())!!
        assertEquals(7, parsed.revision)
        assertEquals(CameraMode.ORBIT, parsed.request.mode)
        assertEquals(1f, parsed.request.anchor.x)
        assertEquals(4f, parsed.request.radius)
        assertEquals(-0.25f, parsed.request.elevation)
        assertEquals(0.125f, parsed.request.orbitRadiansPerSecond)
    }

    @Test fun rejectsOverflowBeforeNarrowing() {
        assertThrows(IllegalArgumentException::class.java) { parseCameraProp(prop("revision" to 2147483648.0)) }
        assertThrows(IllegalArgumentException::class.java) { parseCameraProp(prop("mode" to 2147483648.0)) }
        assertThrows(IllegalArgumentException::class.java) { parseCameraProp(prop("anchorX" to Double.MAX_VALUE)) }
        assertThrows(IllegalArgumentException::class.java) { parseCameraProp(prop("radius" to Double.NaN)) }
    }

    @Test fun rejectsMalformedBodyWithRecoverableRevision() {
        assertThrows(IllegalArgumentException::class.java) { parseCameraProp(prop("mode" to 2.0)) }
        assertThrows(IllegalArgumentException::class.java) { parseCameraProp(prop("radius" to 0.0)) }
        val missing = prop().apply { remove("azimuth") }
        assertThrows(IllegalArgumentException::class.java) { parseCameraProp(missing) }
        assertEquals(7, cameraPropRevision(missing))
        assertEquals(0, cameraPropRevision(prop("revision" to 2147483648.0)))
    }

    @Test fun zeroRevisionIsNoTransactionAndNegativeIsInvalid() {
        assertNull(parseCameraProp(prop("revision" to 0.0)))
        assertThrows(IllegalArgumentException::class.java) { parseCameraProp(prop("revision" to -1.0)) }
    }
}
