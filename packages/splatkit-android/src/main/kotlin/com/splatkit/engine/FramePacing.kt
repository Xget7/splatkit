package com.splatkit.engine

/** The most frames a second the render loop draws, whatever the panel's refresh rate. */
internal const val MAX_FRAMES_PER_SECOND = 60

private const val NANOS_PER_SECOND = 1_000_000_000L

// Vsync timestamps wobble around the panel period, so a 60 Hz tick that lands slightly early
// must still draw instead of halving the rate to 30.
private const val VSYNC_JITTER_NANOS = 2_000_000L

private const val MIN_FRAME_NANOS = NANOS_PER_SECOND / MAX_FRAMES_PER_SECOND - VSYNC_JITTER_NANOS

/**
 * True when a vsync at [frameTimeNanos] is far enough past the last drawn frame to draw again.
 * A 120 Hz panel then draws every other tick, which halves the GPU work of a view that rarely moves.
 */
internal fun isFrameDue(frameTimeNanos: Long, lastFrameNanos: Long): Boolean =
    frameTimeNanos - lastFrameNanos >= MIN_FRAME_NANOS
