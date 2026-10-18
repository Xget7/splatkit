package com.splatkit.reactnative

import com.splatkit.DepthRange
import com.splatkit.LensSettings
import org.junit.Assert.*
import org.junit.Test
import java.io.File

class WorldSessionTest {
    @Test fun replacementRejectsOldCallbacksAndReadinessMustFollowUpload() {
        val session = WorldSession()
        val old = session.replace()
        assertFalse(session.accept(old, "frameReady"))
        assertTrue(session.accept(old, "uploaded"))
        val current = session.replace()
        assertFalse(session.accept(old, "frameReady"))
        assertFalse(session.accept(old, "failed"))
        assertTrue(session.accept(current, "uploaded"))
        assertFalse(session.accept(current, "uploaded"))
        assertTrue(session.accept(current, "frameReady"))
        assertFalse(session.accept(current, "frameReady"))
    }

    @Test fun failureIsTerminalAndStatsAreAtMostTwoHzAfterReadiness() {
        val session = WorldSession()
        val token = session.replace()
        assertFalse(session.sample(0))
        session.accept(token, "uploaded")
        assertFalse(session.sample(500))
        session.accept(token, "frameReady")
        assertTrue(session.sample(500))
        assertFalse(session.sample(999))
        assertTrue(session.sample(1000))
        assertTrue(session.accept(token, "failed"))
        assertFalse(session.accept(token, "uploaded"))
        assertFalse(session.sample(1500))
        session.replace()
        assertFalse(session.sample(2000))
    }

    @Test fun validatesLocalFilesAndNativeCapacityBounds() {
        val file = File.createTempFile("splat-request", ".spz")
        try {
            val valid = WorldRequest("request", file.absolutePath, WorldKind.FILE, 3, 2_200_000, 8_000_000)
            valid.validate()
            valid.copy(kind = WorldKind.TILESET).validate()
            listOf(valid.copy(requestId = " "), valid.copy(filePath = "https://world.spz"),
                valid.copy(filePath = "/"), valid.copy(filePath = file.absolutePath + "missing"),
                valid.copy(kind = -1), valid.copy(kind = 2),
                valid.copy(maxShDegree = 4), valid.copy(lodCapacitySplats = 2_200_001),
                valid.copy(residencyCapacitySplats = 99_999)).forEach {
                assertThrows(IllegalArgumentException::class.java) { it.validate() }
            }
        } finally { file.delete() }
    }

    @Test fun anAbsentLensKeepsTheDefaultAndASetOneIsInRange() {
        val file = File.createTempFile("splat-request", ".spz")
        try {
            val valid = WorldRequest("request", file.absolutePath, WorldKind.FILE, 3, 0, 2_000_000)
            valid.validate()
            assertEquals(LensSettings(), valid.lens)
            val set = valid.copy(fieldOfViewDegrees = 90f, nearPlane = 0.5f, farPlane = 50_000f)
            set.validate()
            assertEquals(LensSettings(90f, DepthRange(0.5f, 50_000f)), set.lens)
            listOf(valid.copy(fieldOfViewDegrees = 9.9f), valid.copy(fieldOfViewDegrees = 120.1f),
                valid.copy(fieldOfViewDegrees = Float.NaN), valid.copy(nearPlane = 0.05f),
                valid.copy(farPlane = 1000f), valid.copy(nearPlane = -1f, farPlane = 10f),
                valid.copy(nearPlane = 2f, farPlane = 1f), valid.copy(nearPlane = 1f, farPlane = Float.NaN),
                valid.copy(nearPlane = 1f, farPlane = Float.POSITIVE_INFINITY),
                valid.copy(nearPlane = 0.5f, farPlane = 50_001f)).forEach {
                assertThrows(IllegalArgumentException::class.java) { it.validate() }
            }
        } finally { file.delete() }
    }
}
