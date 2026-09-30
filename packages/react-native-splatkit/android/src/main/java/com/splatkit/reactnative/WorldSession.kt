package com.splatkit.reactnative

import java.io.File

// What the Vulkan backend accepts, which conservativeCapabilities in performance.ts must fit.
private const val MAX_LOD_CAPACITY_SPLATS = 2_200_000
private const val MIN_RESIDENCY_CAPACITY_SPLATS = 100_000
private const val MAX_RESIDENCY_CAPACITY_SPLATS = 8_000_000
private const val MAX_SH_DEGREE = 3

/** Stats leave the adapter at most twice a second. */
internal const val STATS_INTERVAL_MILLIS = 500L

private fun requireRequestId(requestId: String) {
    require(requestId.isNotBlank()) { "requestId must be nonempty" }
}

private fun requireLocalPath(filePath: String) {
    require(filePath.startsWith('/') && !filePath.startsWith("//") &&
        !filePath.contains('\u0000') && filePath != "/") { "filePath must be an absolute local file" }
}

private fun requireReadableFile(filePath: String) {
    require(File(filePath).isFile && File(filePath).canRead()) { "filePath must name a readable file" }
}

internal data class WorldRequest(
    val requestId: String,
    val filePath: String,
    val maxShDegree: Int,
    val lodCapacitySplats: Int,
    val residencyCapacitySplats: Int,
) {
    fun validate() {
        requireRequestId(requestId)
        requireLocalPath(filePath)
        require(maxShDegree in 0..MAX_SH_DEGREE) { "maxShDegree must be in 0..$MAX_SH_DEGREE" }
        require(lodCapacitySplats in 0..MAX_LOD_CAPACITY_SPLATS) {
            "lodCapacitySplats must be in 0..$MAX_LOD_CAPACITY_SPLATS"
        }
        require(residencyCapacitySplats in MIN_RESIDENCY_CAPACITY_SPLATS..MAX_RESIDENCY_CAPACITY_SPLATS) {
            "residencyCapacitySplats must be in $MIN_RESIDENCY_CAPACITY_SPLATS..$MAX_RESIDENCY_CAPACITY_SPLATS"
        }
        requireReadableFile(filePath)
    }
}

internal data class ColliderRequest(val requestId: String, val filePath: String) {
    fun validate() {
        requireRequestId(requestId)
        requireLocalPath(filePath)
        requireReadableFile(filePath)
    }
}

/** One engine per transaction. Tokens suppress callbacks queued before replacement/release. */
internal class WorldSession {
    var generation = 0L
        private set
    var uploaded = false
        private set
    var frameReady = false
        private set
    private var failed = false
    private var lastStatsAt: Long? = null

    fun replace(): Long {
        generation++
        uploaded = false
        frameReady = false
        failed = false
        lastStatsAt = null
        return generation
    }

    fun accept(token: Long, phase: String): Boolean {
        if (token != generation || failed) return false
        return when (phase) {
            WorldPhase.UPLOADED -> if (uploaded) false else { uploaded = true; true }
            WorldPhase.FRAME_READY -> if (!uploaded || frameReady) false else { frameReady = true; true }
            WorldPhase.FAILED -> { failed = true; true }
            else -> false
        }
    }

    fun sample(nowMillis: Long): Boolean {
        val tooSoon = lastStatsAt?.let { nowMillis - it < STATS_INTERVAL_MILLIS } == true
        if (!frameReady || failed || tooSoon) return false
        lastStatsAt = nowMillis
        return true
    }
}
