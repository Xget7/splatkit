import SwiftUI
import UIKit

struct ContentView: View {
    @StateObject private var session = SplatSession()
    @Environment(\.scenePhase) private var scenePhase
    @State private var walkSpeed: Float = 1.5

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
            if session.scenePrepared && !session.isScripted {
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
            UIApplication.shared.isIdleTimerDisabled = phase == .active && session.stoppedReason == nil && session.isScripted
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
            Button("capture") { session.capture() }
        }
        .buttonStyle(.bordered)
        .tint(.white)
    }

    private func start() {
        guard let runFile = session.runFile, session.stoppedReason == nil else { return }
        UIApplication.shared.isIdleTimerDisabled = scenePhase == .active && session.isScripted
        let view = session.view
        let world = runFile.world ?? .interactive
        let issMap = world.file.lowercased().contains(RunFile.World.issName)
        // A pose holds the camera where the run file puts it.
        let issShot = issMap && !session.isScripted && runFile.pose == nil
        session.orbit?.end()
        session.orbit = nil
        view.touchLookEnabled = !session.isScripted && !issShot

        // RunFile checks the letters; the SDK decides whether they name a frame.
        let sourceFrame = world.sourceFrame.flatMap(CoordinateFrame.init)
        if let letters = world.sourceFrame, sourceFrame == nil {
            NSLog("SplatSourceFrameInvalid: %@", letters)
            session.reject("world.source_frame: \(letters) names no frame")
            return
        }
        view.sourceFrame = sourceFrame
        if let url = AppFiles.resolve(world.file) {
            if world.streamsTiles {
                view.loadTiledWorld(tileset: url)
            } else {
                view.loadWorld(file: url)
            }
        } else {
            session.status = "World not found: \(world.file)"
            NSLog("SplatWorldMissing: %@", world.file)
        }
        if let collider = world.collider {
            if let url = AppFiles.resolve(collider) {
                view.loadCollider(file: url)
            } else {
                session.status = "Collider not found: \(collider)"
                NSLog("SplatWorldMissing: %@", collider)
            }
        }
        // Broadside to the truss, far enough to clear the solar arrays.
        if issMap {
            view.lookAt(from: SIMD3<Float>(0, 35, 128), target: SIMD3<Float>(0, -2, -2), up: SIMD3<Float>(0, 1, 0))
        } else {
            view.cameraPose = CameraPose(x: 0, y: 0, z: 0, yaw: 0, pitch: 0)
        }
        if let pose = runFile.pose {
            view.cameraPose = CameraPose(x: pose.x, y: pose.y, z: pose.z, yaw: pose.yaw, pitch: pose.pitch)
        }
        view.setMotionEnabled(false)
        session.motion = view.isMotionEnabled
        if issShot {
            session.orbit = OrbitPath(view: view, settings: OrbitPath.Settings(pivot: SIMD3<Float>(0, -2, -2)), shot: session.shot)
        }
        if let run = runFile.run, run.orbit == nil {
            view.startBenchmark(seconds: Float(run.seconds))
        }
        session.resume()
    }
}

private extension RunFile.World {
    /// A World Labs export, stored in rdf without saying so.
    static let interactive = RunFile.World(file: "kitchen_500k.spz", sourceFrame: "rdf")
    /// Worlds whose file name holds this get the station's framing and camera shots.
    static let issName = "iss"
}
