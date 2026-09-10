import SwiftUI

struct ContentView: View {
    @StateObject private var session = SplatSession()
    @Environment(\.scenePhase) private var scenePhase
    @State private var walkSpeed: Float = 1.5
    @State private var captureMessage = ""
    private let args = LaunchArgs()

    var body: some View {
        ZStack(alignment: .topLeading) {
            SplatViewHost(session: session).ignoresSafeArea()
            hud
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
        .onAppear(perform: start)
        .onChange(of: scenePhase) { _, phase in
            switch phase {
            case .active: session.view.resume(); session.startPolling()
            case .inactive, .background: session.view.pause(); session.stopPolling()
            @unknown default: break
            }
        }
    }

    private var hud: some View {
        let s = session.stats
        return VStack(alignment: .leading, spacing: 2) {
            Text(session.gpu)
            Text(session.status)
            Text(String(format: "%.0f fps  frame %.1f ms  gpu %.1f ms  sort %.0f ms", s.fps, s.frameMillis, s.gpuMillis, s.sortMillis))
            Text("\(s.splatCount) drawn  \(s.walking ? "walk" : "fly")  \(s.motion ? "gyro" : "touch")")
            if !captureMessage.isEmpty { Text(captureMessage) }
        }
        .font(.system(size: 12, weight: .medium, design: .monospaced))
        .foregroundColor(.white)
        .padding(8)
        .background(Color.black.opacity(0.4))
        .cornerRadius(6)
        .padding(.top, 54)
        .padding(.leading, 12)
        .allowsHitTesting(false)
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
        let view = session.view
        if let v = args.float("scale") { view.renderScale = v }
        if let v = args.int("sh") { view.maxShDegree = v }
        if let v = args.int("shdraw") { view.shDegree = v }
        if let v = args.int("budget") { view.splatBudget = v }
        if let v = args.int("residency") { view.residencyBudget = v }
        if let v = args.float("margin") { view.cullMarginDegrees = v }
        if args.has("linear") { view.linearBlending = args.bool("linear") ?? true }

        if let tileset = args.string("tileset"), let url = LaunchArgs.resolve(tileset) {
            view.loadTiledWorld(tileset: url)
        } else if let url = LaunchArgs.resolve(args.string("world") ?? "kitchen_500k.spz") {
            view.loadWorld(file: url)
        } else {
            session.status = "world not found"
        }
        if let collider = args.string("collider") ?? (args.has("world") || args.has("tileset") ? nil : "kitchen_collider.glb"),
           let url = LaunchArgs.resolve(collider) {
            view.loadCollider(file: url)
        }
        if let pose = args.string("pose") {
            let p = pose.split(separator: ",").compactMap { Float($0.trimmingCharacters(in: .whitespaces)) }
            if p.count >= 3 {
                view.cameraPose = CameraPose(x: p[0], y: p[1], z: p[2], yaw: p.count > 3 ? p[3] : 0, pitch: p.count > 4 ? p[4] : 0)
            }
        }
        view.setMotionEnabled(args.bool("gyro") ?? !(args.has("benchmark") || args.has("capture")))
        session.motion = view.isMotionEnabled
        if let v = args.float("walk") { view.setWalkVelocity(forward: v, right: 0) }
        if args.has("benchmark") {
            view.startBenchmark(seconds: args.float("benchmark") ?? 10)
        }
        if let delay = args.float("capture") {
            DispatchQueue.main.asyncAfter(deadline: .now() + Double(delay)) { capture() }
        }
        view.resume()
        session.startPolling()
    }

    private func capture() {
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let file = documents.appendingPathComponent("capture.png")
        session.view.captureFrame(to: file) { ok in
            captureMessage = ok ? "captured capture.png" : "capture failed"
        }
    }
}
