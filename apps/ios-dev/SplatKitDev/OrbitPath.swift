import QuartzCore
import simd

/// A scripted camera path for demos: the camera circles a pivot about an axis at a
/// constant angular speed, always looking at the pivot, while the radius breathes in and
/// out (the zoom). The top of the frame is the axis or the circle's tangent, both
/// continuous through an orbit that passes over and under the scene. Every frame the pose is teleported,
/// so the gyroscope must be off.
final class OrbitPath {
    struct Settings {
        var pivot: SIMD3<Float>
        var axis = SIMD3<Float>(1, 0, 0)  // the circle is perpendicular to it
        var radius: Float = 10            // meters at the widest
        var degreesPerSecond: Float = 12
        var zoom: Float = 0.3             // fraction of the radius the zoom comes in by
        var zoomPeriod: Float = 0         // seconds per zoom in and out; 0 means one turn
        var startDegrees: Float = 0
        // What is at the top of the frame: the orbit's axis (the scene spins about the
        // screen's vertical, right for a portrait phone) or the circle's tangent (the
        // scene rolls past, like flying over it).
        var topIsAxis = true
    }

    private let view: SplatMetalView
    private let settings: Settings
    private let axis: SIMD3<Float>
    private let e1: SIMD3<Float>
    private let e2: SIMD3<Float>
    private var link: CADisplayLink?
    private var start: CFTimeInterval = 0

    init(view: SplatMetalView, settings: Settings) {
        self.view = view
        self.settings = settings
        let axis = simd_normalize(settings.axis)
        self.axis = axis
        // The circle's basis: as close to world up as the axis allows, and its cross.
        var first = SIMD3<Float>(0, 1, 0) - axis * simd_dot(SIMD3<Float>(0, 1, 0), axis)
        if simd_length(first) < 1e-4 { first = SIMD3<Float>(0, 0, 1) - axis * axis.z }
        e1 = simd_normalize(first)
        e2 = simd_normalize(simd_cross(axis, e1))
    }

    func begin() {
        start = CACurrentMediaTime()
        link = CADisplayLink(target: self, selector: #selector(tick))
        link?.add(to: .main, forMode: .common)
        tick()
    }

    func end() {
        link?.invalidate()
        link = nil
    }

    @objc private func tick() {
        let t = Float(CACurrentMediaTime() - start)
        let s = settings
        let turn = 360 / max(s.degreesPerSecond, 0.01)
        let period = s.zoomPeriod > 0 ? s.zoomPeriod : turn
        // Radius eases from the full radius in to (1 - zoom) of it and back, once per period.
        let phase = (1 - cos(2 * Float.pi * t / period)) / 2
        let radius = s.radius * (1 - s.zoom * phase)
        let angle = (s.startDegrees + s.degreesPerSecond * t) * Float.pi / 180
        let outward = cos(angle) * e1 + sin(angle) * e2
        let tangent = -sin(angle) * e1 + cos(angle) * e2
        view.lookAt(from: s.pivot + radius * outward, target: s.pivot, up: s.topIsAxis ? axis : tangent)
    }
}
