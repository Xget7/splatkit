package com.splatkit.reactnative

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
            val valid = WorldRequest("request", file.absolutePath, 3, 2_200_000, 8_000_000)
            valid.validate()
            listOf(valid.copy(requestId = " "), valid.copy(filePath = "https://world.spz"),
                valid.copy(filePath = "/"), valid.copy(filePath = file.absolutePath + "missing"),
                valid.copy(maxShDegree = 4), valid.copy(lodCapacitySplats = 2_200_001),
                valid.copy(residencyCapacitySplats = 99_999)).forEach {
                assertThrows(IllegalArgumentException::class.java) { it.validate() }
            }
        } finally { file.delete() }
    }
}
