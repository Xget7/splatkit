package com.splatkit.engine

import org.junit.Assert.assertEquals
import org.junit.Test

class FramePacingTest {
    @Test fun aFastPanelDrawsAtMostSixtyFramesASecond() {
        assertEquals(MAX_FRAMES_PER_SECOND, framesDrawnInOneSecond(PANEL_120_HZ, jitterNanos = 0))
    }

    @Test fun aSixtyHertzPanelDrawsEveryTickEvenWhenOneArrivesEarly() {
        assertEquals(PANEL_60_HZ, framesDrawnInOneSecond(PANEL_60_HZ, jitterNanos = EARLY_TICK_NANOS))
    }

    /** Runs the pacing over one second of vsync ticks, every other tick arriving [jitterNanos] early. */
    private fun framesDrawnInOneSecond(refreshHz: Int, jitterNanos: Long): Int {
        val period = NANOS_PER_SECOND / refreshHz
        var lastDrawn = START_NANOS - NANOS_PER_SECOND
        var drawn = 0
        for (tick in 0 until refreshHz) {
            val early = if (tick % 2 == 1) jitterNanos else 0L
            val frameTime = START_NANOS + tick * period - early
            if (isFrameDue(frameTime, lastDrawn)) {
                lastDrawn = frameTime
                drawn++
            }
        }
        return drawn
    }

    private companion object {
        const val NANOS_PER_SECOND = 1_000_000_000L
        const val PANEL_60_HZ = 60
        const val PANEL_120_HZ = 120
        const val EARLY_TICK_NANOS = 500_000L
        // Choreographer timestamps come from System.nanoTime, far from zero.
        const val START_NANOS = 5_000_000_000L
    }
}
