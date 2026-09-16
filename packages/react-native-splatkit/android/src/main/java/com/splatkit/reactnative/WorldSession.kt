package com.splatkit.reactnative

import java.io.File

internal data class WorldRequest(
    val requestId: String,
    val filePath: String,
    val maxShDegree: Int,
    val lodCapacitySplats: Int,
    val residencyCapacitySplats: Int,
) {
    fun validate() {
        require(requestId.isNotBlank()) { "requestId must be nonempty" }
        require(filePath.startsWith('/') && !filePath.startsWith("//") &&
            !filePath.contains('\u0000') && filePath != "/") { "filePath must be an absolute local file" }
        require(maxShDegree in 0..3) { "maxShDegree must be in 0..3" }
        require(lodCapacitySplats in 0..2_200_000) { "lodCapacitySplats must be in 0..2200000" }
        require(residencyCapacitySplats in 100_000..8_000_000) { "residencyCapacitySplats must be in 100000..8000000" }
        require(File(filePath).isFile && File(filePath).canRead()) { "filePath must name a readable file" }
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
            "uploaded" -> if (uploaded) false else { uploaded = true; true }
            "frameReady" -> if (!uploaded || frameReady) false else { frameReady = true; true }
            "failed" -> { failed = true; true }
            else -> false
        }
    }

    fun sample(nowMillis: Long): Boolean {
        if (!frameReady || failed || lastStatsAt?.let { nowMillis - it < 500 } == true) return false
        lastStatsAt = nowMillis
        return true
    }
}
