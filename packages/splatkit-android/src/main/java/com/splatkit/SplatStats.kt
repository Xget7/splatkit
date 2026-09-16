package com.splatkit

/**
 * Published snapshot, refreshed twice a second; completed-frame diagnostics may lag the view.
 * Counts cross JNI as floats: all integers through 16,777,216 are exact; larger values may round.
 */
class SplatStats {
    var fps = 0f
    var frameMillis = 0f
    /** GPU time of the last frame from timestamp queries; zero when unsupported. */
    var gpuMillis = 0f
    /** Sort duration; the GPU path reports zero when timestamp queries are unavailable. */
    var sortMillis = 0f
    /** Loaded source splats, not the number currently drawn or GPU-resident tree records. */
    var splatCount = 0
    /** Alias of [splatCount], with the same source-count meaning. */
    var loadedSplatCount: Int
        get() = splatCount
        set(value) { splatCount = value }
    var walking = false
    var motion = false
    /** Last completed visibility/order result; zero remains zero even when a source is loaded. */
    var drawnSplatCount = 0
    /** Completed compute screen tiles, including background-only tiles; zero when unavailable. */
    var computeTileCount = 0
    /** Completed compute screen tiles containing candidates; zero when unavailable. */
    var nonemptyComputeTileCount = 0
    /** Completed hardware screen tiles; zero when unavailable. */
    var hardwareTileCount = 0
}

internal const val SPLAT_STATS_FLOATS = 11

/** Mirrors nativeStats: the legacy seven floats stay first, followed by four completed counts. */
internal fun decodeSplatStats(values: FloatArray, into: SplatStats): SplatStats {
    require(values.size >= SPLAT_STATS_FLOATS) { "Stats payload needs $SPLAT_STATS_FLOATS floats" }
    into.fps = values[0]
    into.frameMillis = values[1]
    into.gpuMillis = values[2]
    into.sortMillis = values[3]
    into.splatCount = values[4].toInt()
    into.walking = values[5] != 0f
    into.motion = values[6] != 0f
    into.drawnSplatCount = values[7].toInt()
    into.computeTileCount = values[8].toInt()
    into.nonemptyComputeTileCount = values[9].toInt()
    into.hardwareTileCount = values[10].toInt()
    return into
}
