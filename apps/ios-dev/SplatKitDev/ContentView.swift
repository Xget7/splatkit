import SwiftUI
import UIKit

struct ContentView: View {
    @StateObject private var session = SplatSession()
    @Environment(\.scenePhase) private var scenePhase
    @State private var walkSpeed: Float = 1.5
    @State private var captureMessage = ""
    private let args = LaunchArgs()

    var body: some View {
        ZStack(alignment: .topLeading) {
            SplatViewHost(session: session).ignoresSafeArea()
            if !session.scenePrepared {
                Color.black.ignoresSafeArea()
                VStack(spacing: 12) {
                    if !session.loadingFailed && session.stoppedReason == nil {
                        ProgressView().tint(.white)
                    }
                    Text(session.status).foregroundStyle(.white)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .accessibilityIdentifier("splat.preparing")
            }
            if session.scenePrepared && (args.bool("hud") ?? true) {
                overlay
            }
            // The SDK ships no walking control; this is the app's own, over the SDK's view.
            if session.walking {
                VStack {
                    Spacer()
                    HStack(alignment: .bottom) {
                        Joystick { forward, right in
                            session.view.setWalkVelocity(forward: forward * walkSpeed, right: right * walkSpeed)
                        }
                        Spacer()
                        controls
                    }
                    .padding(24)
                }
            }
        }
        .onAppear(perform: start)
        .onDisappear { UIApplication.shared.isIdleTimerDisabled = false }
        .onChange(of: scenePhase) { _, phase in
            UIApplication.shared.isIdleTimerDisabled = phase == .active && session.stoppedReason == nil && (args.bool("keep-awake") ?? false)
            switch phase {
            case .active: session.resume()
            case .inactive, .background: session.pause()
            @unknown default: break
            }
        }
    }

    private var shotSelection: Binding<CameraShot?> {
        Binding(get: { session.shot }, set: { if let shot = $0 { session.shot = shot } })
    }

    private var overlay: some View {
        VStack(alignment: .leading) {
            RenderStatsCard(session: session)
            Spacer()
            VStack(spacing: 6) {
                if session.orbit != nil {
                    CapsulePicker(items: CameraShot.allCases, selection: shotSelection) { $0.title }
                }
                CapsulePicker(items: QualityTier.allCases, selection: $session.quality) { $0.title }
            }
            .frame(maxWidth: .infinity)
        }
        .padding(.horizontal, 16)
        .padding(.top, 4)
        .padding(.bottom, 8)
        .transition(.opacity)
    }

    private var controls: some View {
        VStack(spacing: 12) {
            Button(session.motion ? "gyro" : "touch") {
                session.view.setMotionEnabled(!session.view.isMotionEnabled)
                session.motion = session.view.isMotionEnabled
            }
            Button("capture") { capture() }
        }
        .buttonStyle(.bordered)
        .tint(.white)
    }

    private func start() {
        UIApplication.shared.isIdleTimerDisabled = scenePhase == .active && (args.bool("keep-awake") ?? false)
        let view = session.view
        let defaultWorld = "kitchen_500k.spz"
        let requestedMap = (args.string("tileset") ?? args.string("world") ?? defaultWorld).lowercased()
        let issMap = requestedMap.contains("iss")
        let orbitEnabled = issMap && (args.bool("orbit") ?? true)
            && !args.has("pose") && !args.has("benchmark") && !args.has("walk")
        session.orbit?.end()
        session.orbit = nil
        // Change framing with camera distance, not render resolution or field of view.
        view.renderScale = args.float("scale") ?? 1
        view.touchLookEnabled = !orbitEnabled
        view.motionToggleEnabled = !issMap
        view.maxShDegree = args.int("sh") ?? 1
        if let v = args.int("shdraw") { view.shDegree = v }
        // Explicit renderer switches win; otherwise start at a quality level, High by default.
        let customQuality = ["scale", "shdraw", "depth-key-bits", "min-pixel-radius", "lod-error-pixels", "lod-splat-limit"].contains(where: args.has)
        if !customQuality {
            session.quality = args.string("quality").flatMap(QualityTier.init(rawValue:)) ?? .high
        }
        if let v = args.int("budget") { view.splatBudget = v }
        if let v = args.int("residency") { view.residencyBudget = v }
        if let v = args.float("margin") { view.cullMarginDegrees = v }
        if args.has("linear") { view.linearBlending = args.bool("linear") ?? true }

        if let tileset = args.string("tileset"), let url = LaunchArgs.resolve(tileset) {
            view.loadTiledWorld(tileset: url)
        } else if let world = args.string("world"), let url = LaunchArgs.resolve(world) {
            view.loadWorld(file: url)
        } else if let url = LaunchArgs.resolve(defaultWorld) {
            view.loadWorld(file: url)
        } else {
            session.status = "World not found: \(args.string("tileset") ?? args.string("world") ?? defaultWorld)"
        }
        if let collider = args.string("collider"),
           let url = LaunchArgs.resolve(collider) {
            view.loadCollider(file: url)
        }
        // Broadside to the truss, far enough to clear the solar arrays.
        if issMap {
            view.lookAt(from: SIMD3<Float>(0, 35, 128), target: SIMD3<Float>(0, -2, -2), up: SIMD3<Float>(0, 1, 0))
        } else {
            view.cameraPose = CameraPose(x: 0, y: 0, z: 0, yaw: 0, pitch: 0)
        }
        if let pose = args.string("pose") {
            let p = pose.split(separator: ",").compactMap { Float($0.trimmingCharacters(in: .whitespaces)) }
            if p.count >= 3 {
                view.cameraPose = CameraPose(x: p[0], y: p[1], z: p[2], yaw: p.count > 3 ? p[3] : 0, pitch: p.count > 4 ? p[4] : 0)
            }
        }
        if issMap {
            view.setMotionEnabled(false)
        } else {
            view.setMotionEnabled(args.bool("gyro") ?? false)
        }
        session.motion = view.isMotionEnabled
        if orbitEnabled {
            var settings = OrbitPath.Settings(pivot: SIMD3<Float>(0, -2, -2))
            if let radius = args.float("radius"), radius.isFinite, radius > 0 { settings.radius = radius }
            if let speed = args.float("speed"), speed.isFinite { settings.degreesPerSecond = speed }
            if let start = args.float("start"), start.isFinite { settings.startDegrees = start }
            let shot = args.string("shot").flatMap(CameraShot.init(rawValue:)) ?? .orbit
            session.shot = shot
            let orbit = OrbitPath(view: view, settings: settings, shot: shot)
            session.orbit = orbit
        }
        if let v = args.float("walk") { view.setWalkVelocity(forward: v, right: 0) }
        if args.has("benchmark") {
            view.startBenchmark(seconds: args.float("benchmark") ?? 10)
        }
        if let delay = args.float("capture") {
            DispatchQueue.main.asyncAfter(deadline: .now() + Double(delay)) { capture() }
        }
        session.resume()
    }

    private func capture() {
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let file = documents.appendingPathComponent("capture.png")
        session.view.captureFrame(to: file) { ok in
            captureMessage = ok ? "captured capture.png" : "capture failed"
        }
    }
}
