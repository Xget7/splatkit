package com.splatkit

/** A snapshot of what the engine is doing, refreshed twice a second. */
class SplatStats {
    var fps = 0f
    var frameMillis = 0f
    var sortMillis = 0f
    var splatCount = 0
    var walking = false
    var motion = false
}
