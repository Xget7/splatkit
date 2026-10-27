import QuartzCore
import simd

/// Camera shots around the station. Every shot keeps the station's own up axis (+Y) up, so
/// the camera never rolls; they differ in the path the eye follows, its height and pace.
enum CameraShot: String, CaseIterable, Identifiable {
    case overview, orbit, detail, flyby, tour

    var id: String { rawValue }

    var title: String {
        switch self {
        case .overview: "Overview"
        case .orbit: "Orbit"
        case .detail: "Detail"
        case .flyby: "Flyby"
        case .tour: "Tour"
        }
    }
}

/// Constant field of view; the camera moves. The station's plan lies in the XZ plane, with the
/// long truss on X and the modules on Z. The eye circles the vertical axis on an ellipse
/// whose semi-axes are X and Z, so a close pass can hug the modules and swing wide of the
/// solar arrays at the truss ends. Switching shots eases every parameter over a few seconds,
/// so the camera never jumps.
final class OrbitPath {
    struct Settings {
        var pivot: SIMD3<Float>
        /// Orbit shot radius, at least `minimumRadius`.
        var radius: Float = 135
        var degreesPerSecond: Float = 5
        /// Azimuth about +Y; 0 puts the camera on +Z, broadside to the truss.
        var startDegrees: Float = 20
    }

    /// Any circle this wide clears the arrays, whose far corners are 88 m from the pivot.
    static let minimumRadius: Float = 100

    /// One instant of a shot.
    private struct Pose {
        /// Semi-axes of the eye's ellipse along the truss (X) and across it (Z).
        var alongTruss: Float
        var acrossTruss: Float
        /// Degrees above the station's plane.
        var elevation: Float
        var degreesPerSecond: Float
        /// Offset of the look target along the truss.
        var drift: Float

        static func circle(_ radius: Float, elevation: Float, degreesPerSecond: Float) -> Pose {
            Pose(alongTruss: radius, acrossTruss: radius, elevation: elevation,
                 degreesPerSecond: degreesPerSecond, drift: 0)
        }

        static func mix(_ a: Pose, _ b: Pose, _ t: Float) -> Pose {
            Pose(alongTruss: simd_mix(a.alongTruss, b.alongTruss, t),
                 acrossTruss: simd_mix(a.acrossTruss, b.acrossTruss, t),
                 elevation: simd_mix(a.elevation, b.elevation, t),
                 degreesPerSecond: simd_mix(a.degreesPerSecond, b.degreesPerSecond, t),
                 drift: simd_mix(a.drift, b.drift, t))
        }
    }

    // CADisplayLink retains its target. A weak proxy lets the session release this path.
    private final class TickTarget: NSObject {
        weak var owner: OrbitPath?
        @objc func tick(_ link: CADisplayLink) { owner?.tick(link) }
    }

    private static let transitionSeconds: Float = 3
    /// Tour order and how long each leg holds before easing into the next.
    private static let tourLegs: [(CameraShot, Float)] = [(.overview, 20), (.orbit, 24), (.detail, 36), (.flyby, 30)]

    private let view: SplatMetalView
    private let settings: Settings
    private let target = TickTarget()
    private var link: CADisplayLink?
    private var lastTimestamp: CFTimeInterval?
    /// Azimuth in radians, advanced by the blended pace so a change of speed never jumps.
    private var azimuth: Float
    /// Seconds of motion, paused while the path is not running.
    private var clock: Float = 0
    private(set) var shot: CameraShot
    private var from: Pose
    private var shotStarted: Float = 0

    init(view: SplatMetalView, settings: Settings, shot: CameraShot = .orbit) {
        self.view = view
        var settings = settings
        settings.radius = max(settings.radius, Self.minimumRadius)
        self.settings = settings
        self.shot = shot
        azimuth = settings.startDegrees * .pi / 180
        from = .circle(settings.radius, elevation: 16, degreesPerSecond: settings.degreesPerSecond)
        target.owner = self
        from = pose(of: shot, at: 0)
        NSLog("SplatOrbit: shot=%@ radius=%.2f speed=%.2f start=%.2f", shot.rawValue,
              settings.radius, settings.degreesPerSecond, settings.startDegrees)
        // Prepare the requested starting pose without advancing time during loading.
        applyPose()
    }

    deinit { end() }

    func begin() {
        guard link == nil else { return }
        lastTimestamp = nil
        applyPose()
        let link = CADisplayLink(target: target, selector: #selector(TickTarget.tick(_:)))
        link.preferredFrameRateRange = CAFrameRateRange(minimum: 30, maximum: 120, preferred: 120)
        link.add(to: .main, forMode: .common)
        self.link = link
    }

    func end() {
        link?.invalidate()
        link = nil
        lastTimestamp = nil
    }

    /// Eases from wherever the camera is now into the new shot.
    func setShot(_ next: CameraShot) {
        guard next != shot else { return }
        from = current()
        shot = next
        shotStarted = clock
        NSLog("SplatOrbit: shot=%@", next.rawValue)
    }

    private func tick(_ link: CADisplayLink) {
        if let previous = lastTimestamp {
            // A stalled main thread must not teleport the camera.
            let dt = min(Float(link.timestamp - previous), 0.1)
            clock += dt
            azimuth += dt * current().degreesPerSecond * .pi / 180
            azimuth = azimuth.truncatingRemainder(dividingBy: 2 * .pi)
        }
        lastTimestamp = link.timestamp
        applyPose()
    }

    /// The blended pose: the previous one eased into the shot's own motion.
    private func current() -> Pose {
        let target = pose(of: shot, at: clock - shotStarted)
        return Pose.mix(from, target, Self.ease((clock - shotStarted) / Self.transitionSeconds))
    }

    private static func ease(_ t: Float) -> Float {
        let t = min(max(t, 0), 1)
        return t * t * (3 - 2 * t)
    }

    /// Where a shot wants the camera `elapsed` seconds after it started.
    private func pose(of shot: CameraShot, at elapsed: Float) -> Pose {
        let wave = { (period: Float) in sin(elapsed * 2 * .pi / period) }
        switch shot {
        case .overview:
            // High and far: the whole plan, arrays included, turning slowly.
            return .circle(160, elevation: 38, degreesPerSecond: 4)
        case .orbit:
            return .circle(settings.radius, elevation: 16, degreesPerSecond: settings.degreesPerSecond)
        case .detail:
            // 64 m across the truss hugs the modules; 86 m along it clears the arrays' boxes
            // (|X| 30-66 m, Z -28-45 m) at every azimuth. The eye bobs and the target drifts.
            return Pose(alongTruss: 86, acrossTruss: 64, elevation: 10 + 8 * wave(50),
                        degreesPerSecond: 3, drift: 14 * wave(70))
        case .flyby:
            // Faster, breathing in and out while dipping below the plane and climbing over it.
            let radius = 125 + 25 * wave(30)
            return .circle(radius, elevation: 10 + 25 * wave(26), degreesPerSecond: 8)
        case .tour:
            let loop = Self.tourLegs.reduce(0) { $0 + $1.1 }
            var t = elapsed.truncatingRemainder(dividingBy: loop)
            for (index, leg) in Self.tourLegs.enumerated() {
                if t < leg.1 {
                    let pose = self.pose(of: leg.0, at: t)
                    // Ease the last seconds of a leg into the next leg's opening pose.
                    let next = Self.tourLegs[(index + 1) % Self.tourLegs.count].0
                    let blend = Self.ease((t - (leg.1 - Self.transitionSeconds)) / Self.transitionSeconds)
                    return Pose.mix(pose, self.pose(of: next, at: 0), blend)
                }
                t -= leg.1
            }
            return self.pose(of: .orbit, at: 0)
        }
    }

    private func applyPose() {
        let pose = current()
        let x = pose.alongTruss * sin(azimuth)
        let z = pose.acrossTruss * cos(azimuth)
        let elevation = pose.elevation * .pi / 180
        let eye = SIMD3<Float>(x * cos(elevation), simd_length(SIMD2<Float>(x, z)) * sin(elevation), z * cos(elevation))
        let focus = settings.pivot + SIMD3<Float>(pose.drift, 0, 0)
        view.lookAt(from: settings.pivot + eye, target: focus, up: SIMD3<Float>(0, 1, 0))
    }
}
