package com.splatkit

/**
 * A rendering quality preset: the values [SplatSurfaceView.applyQuality] sets at once.
 * The four presets below are starting points measured on a Xiaomi Mi 9 (Adreno 640);
 * `copy` any of them to change one value, or set the view's properties one by one.
 *
 * Each preset has a reason and a benchmark row in `docs/BENCHMARKS.md`:
 * - [LOW] is for phones that cannot hold 30 fps at [MEDIUM], or for saving battery.
 *   Half resolution, view dependent colour off, and a level of detail budget so that
 *   a scene of any size costs about the same (12.4 ms on the Mi 9 with the 2M splat
 *   house; the budget itself buys under a millisecond there and pays on bigger scenes).
 * - [MEDIUM] holds 60 fps on the Mi 9 with the house (13.4 ms). 0.7 of the resolution
 *   is hard to tell from 1.0 at arm's length, and degree 1 harmonics keep the broad
 *   view dependent tint for a fifth of the harmonics work per vertex.
 * - [HIGH] is the default: the full resolution, every splat, every harmonic, exactly
 *   what the reference rasterizer draws. 19.3 ms on the Mi 9 with the house.
 * - [ULTRA] supersamples at 1.5 times the resolution and keeps a wider margin around
 *   the view, for flagship GPUs or for stills: the thin splats that shimmer at a pixel
 *   each settle, and a flick never shows an empty edge. 39 ms on the Mi 9.
 */
data class RenderQuality(
    /** See [SplatSurfaceView.renderScale]. */
    val renderScale: Float,
    /** See [SplatSurfaceView.shDegree]. */
    val shDegree: Int,
    /** See [SplatSurfaceView.splatBudget]; 0 draws every splat. */
    val splatBudget: Int,
    /** See [SplatSurfaceView.linearBlending]. */
    val linearBlending: Boolean = false,
    /** See [SplatSurfaceView.cullMarginDegrees]. */
    val cullMarginDegrees: Float = 10f,
) {
    companion object {
        val LOW = RenderQuality(renderScale = 0.5f, shDegree = 0, splatBudget = 500_000)
        val MEDIUM = RenderQuality(renderScale = 0.7f, shDegree = 1, splatBudget = 0)
        val HIGH = RenderQuality(renderScale = 1f, shDegree = 3, splatBudget = 0)
        val ULTRA = RenderQuality(renderScale = 1.5f, shDegree = 3, splatBudget = 0, cullMarginDegrees = 20f)

        /** The preset named by [name], case insensitive, or null. */
        fun named(name: String): RenderQuality? = when (name.lowercase()) {
            "low" -> LOW
            "medium" -> MEDIUM
            "high" -> HIGH
            "ultra" -> ULTRA
            else -> null
        }
    }
}
