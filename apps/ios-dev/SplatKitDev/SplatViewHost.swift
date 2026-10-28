import SwiftUI
import UIKit
import Metal
import os

/// The one SplatMetalView of the app, shared with SwiftUI through an observable owner so
/// the HUD can read stats and push settings without recreating the view.
final class SplatSession: ObservableObject {
    /// Spherical harmonics degree the app decodes from every world, whatever the quality level draws.
    private static let maxShDegree = 1
    private let pollInterval = 0.5
    /// 30 seconds of history at the polling rate.
    private let fpsHistoryLength = 60
    /// A test guard, set below the device's Jetsam limit.
    private let memoryLimitBytes: UInt64 = 2800 * 1024 * 1024
    private let minAvailableBytes = 256 * 1024 * 1024
    let view = SplatMetalView()
    /// Nil when the launch arguments were rejected.
    let runFile: RunFile?
    var orbit: OrbitPath?
    @Published var stats = SplatStats()
    @Published var status = "loading"
    @Published var walking = false
    @Published var motion = false
    @Published var scenePrepared = false
    @Published var loadingFailed = false
    /// Frame rate samples taken with the stats, the last `fpsHistoryLength` of them.
    @Published var fpsHistory: [Float] = []
    @Published var quality: QualityTier? {
        didSet { quality?.apply(to: view) }
    }
    @Published var shot: CameraShot = .orbit {
        didSet { orbit?.setShot(shot) }
    }
    private var timer: Timer?
    private var delegateBox: Delegate?
    private var sdkOrbitStartedAt: TimeInterval?
    private var sdkOrbitFps: [Float] = []
    private var sdkOrbitGpuMillis: [Float] = []
    private let resourceDevice: MTLDevice?
    private var memoryWarningObserver: NSObjectProtocol?
    private var firstLoadedTime: TimeInterval?
    private var peakFootprint: UInt64 = 0
    private let preparationStarted = ProcessInfo.processInfo.systemUptime
    private var resumed = false
    private(set) var stoppedReason: StopReason?

    init(arguments: [String] = CommandLine.arguments) {
        let launch = Result { try RunFile(arguments: arguments) }
        runFile = try? launch.get()
        resourceDevice = runFile?.run?.monitor == true ? MTLCreateSystemDefaultDevice() : nil
        let delegate = Delegate(session: self)
        delegateBox = delegate
        view.delegate = delegate
        switch launch {
        case .failure(let error):
            reject(String(describing: error))
        case .success(let runFile):
            NSLog("SplatRun: %@", runFile.compactJSON)
            view.maxShDegree = Self.maxShDegree
            applyRenderer(runFile.renderer)
            if monitorResources { observeMemoryWarnings() }
        }
    }

    deinit {
        if let memoryWarningObserver { NotificationCenter.default.removeObserver(memoryWarningObserver) }
    }

    /// A run section makes a scripted run: no HUD, touch, gyroscope or automatic shot.
    var isScripted: Bool { runFile?.run != nil }

    private var monitorResources: Bool { runFile?.run?.monitor == true }

    private func observeMemoryWarnings() {
        memoryWarningObserver = NotificationCenter.default.addObserver(
            forName: UIApplication.didReceiveMemoryWarningNotification, object: nil, queue: .main
        ) { [weak self] _ in
            // Loading a large world warns at its transient peak and then settles, so a
            // warning only samples now; the footprint and headroom limits decide.
            NSLog("SplatMemoryWarning: sampling resources")
            self?.sampleResources()
        }
    }

    /// Stops the launch before anything loads or runs.
    func reject(_ message: String) {
        stoppedReason = .runInvalid
        status = message
        loadingFailed = true
        NSLog("SplatRunInvalid: %@", message)
    }

    /// The quality level first, then each field the run file gives.
    private func applyRenderer(_ renderer: RunFile.Renderer?) {
        quality = renderer?.quality ?? .high
        if let budget = renderer?.budget { view.splatBudget = budget }
        var policy = view.renderPolicy
        if let pixels = renderer?.subpixel { policy.subpixelThreshold = pixels }
        if let pixels = renderer?.lodErrorPixels { policy.lodErrorPixels = pixels }
        if let limit = renderer?.lodSplatLimit { policy.lodSplatLimit = limit }
        view.renderPolicy = policy
        // BOOL fields of the C structs import as ObjCBool.
        let applied = view.renderPolicy, caps = view.deviceCapabilities
        NSLog("SplatPolicy: %@", "sortDepth=\(applied.sortDepth) subpixelThreshold=\(applied.subpixelThreshold) "
              + "hiZ=\(applied.enableHiZOcclusion.boolValue) "
              + "supports sortDepth=\(caps.policy.sortDepth.boolValue) subpixelThreshold=\(caps.policy.subpixelThreshold.boolValue) "
              + "in [\(caps.policy.minSubpixelThreshold), \(caps.policy.maxSubpixelThreshold)] "
              + "maxLod=\(caps.maxLodCapacitySplats) "
              + "residency=[\(caps.minResidencyCapacitySplats), \(caps.maxResidencyCapacitySplats)]")
    }

    /// Starts what the run section times from the first world frame: the orbit and the capture.
    private func beginScriptedRun() {
        guard let run = runFile?.run else { return }
        if let orbit = run.orbit {
            view.setAnchor(orbit.anchor)
            let speed = abs(orbit.degrees) / Float(run.seconds)
            let accepted = view.animateOrbit(degrees: orbit.degrees, degreesPerSecond: speed)
            NSLog("SplatSDKOrbit: accepted=%d degrees=%.2f speed=%.2f", accepted ? 1 : 0, orbit.degrees, speed)
            if accepted {
                sdkOrbitStartedAt = ProcessInfo.processInfo.systemUptime
            } else {
                stopRun(.orbitRejected)
            }
        }
        if let delay = run.captureAt {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in self?.capture() }
        }
    }

    func capture() {
        let file = AppFiles.documents.appendingPathComponent(AppFiles.captureName)
        view.captureFrame(to: file) { ok in
            NSLog("SplatCapture: %@", ok ? "captured \(AppFiles.captureName)" : "capture failed")
        }
    }

    func resume() {
        guard stoppedReason == nil else { return }
        resumed = true
        if scenePrepared { orbit?.begin() }
        view.resume()
        startPolling()
    }

    func pause() {
        resumed = false
        orbit?.end()
        view.pause()
        stopPolling()
    }

    /// A prepared scene showed no frame in the last window: the engine skips redraws while
    /// nothing moves, and the render loop, which publishes the stats, is still running.
    var isIdle: Bool { scenePrepared && stats.fps == 0 }

    func startPolling() {
        timer?.invalidate()
        timer = Timer.scheduledTimer(withTimeInterval: pollInterval, repeats: true) { [weak self] _ in
            guard let self else { return }
            self.stats = self.view.readStats()
            self.sampleSDKOrbit()
            if self.scenePrepared && !self.isIdle {
                self.fpsHistory.append(self.stats.fps)
                if self.fpsHistory.count > self.fpsHistoryLength { self.fpsHistory.removeFirst(self.fpsHistory.count - self.fpsHistoryLength) }
            }
            self.motion = self.view.isMotionEnabled
            if self.monitorResources { self.sampleResources() }
        }
    }

    func stopPolling() {
        timer?.invalidate()
        timer = nil
    }

    private func sampleSDKOrbit() {
        guard let started = sdkOrbitStartedAt, let duration = runFile?.run?.seconds else { return }
        let elapsed = ProcessInfo.processInfo.systemUptime - started
        if elapsed > 1 && elapsed <= duration {
            sdkOrbitFps.append(stats.fps)
            sdkOrbitGpuMillis.append(stats.gpuMillis)
            NSLog("SplatSDKOrbitSample: elapsed=%.2f fps=%.2f gpuMs=%.2f drawn=%ld",
                  elapsed, stats.fps, stats.gpuMillis, stats.drawnSplatCount)
        }
        if elapsed >= duration + 1 {
            let fps = sdkOrbitFps.reduce(0, +) / Float(max(sdkOrbitFps.count, 1))
            let gpu = sdkOrbitGpuMillis.reduce(0, +) / Float(max(sdkOrbitGpuMillis.count, 1))
            NSLog("SplatSDKOrbitComplete: duration=%.2f samples=%ld meanFps=%.2f meanGpuMs=%.2f",
                  duration, sdkOrbitFps.count, fps, gpu)
            sdkOrbitStartedAt = nil
            stopRun(.orbitComplete)
        }
    }

    /// Dev-only observation at 2 Hz. Rendering and LOD selection remain entirely on GPU.
    /// The guard pauses future submissions; it cannot cancel GPU work already in flight
    /// or guarantee protection against a memory spike between samples.
    private func sampleResources() {
        var info = task_vm_info_data_t()
        var count = mach_msg_type_number_t(MemoryLayout<task_vm_info_data_t>.size / MemoryLayout<integer_t>.size)
        let result = withUnsafeMutablePointer(to: &info) { pointer in
            pointer.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                task_info(mach_task_self_, task_flavor_t(TASK_VM_INFO), $0, &count)
            }
        }
        guard result == KERN_SUCCESS else { stopRun(.memorySampleFailed(result)); return }
        let footprint = UInt64(info.phys_footprint)
        peakFootprint = max(peakFootprint, footprint)
        let available = os_proc_available_memory()
        let thermal = ProcessInfo.processInfo.thermalState
        let now = ProcessInfo.processInfo.systemUptime
        let elapsed = firstLoadedTime.map { now - $0 } ?? -1
        NSLog("SplatResources: elapsed=%.3f footprint=%llu peak=%llu available=%llu metal=%llu thermal=%ld loaded=%ld drawn=%ld paused=%d",
              elapsed, footprint, peakFootprint, UInt64(available), UInt64(resourceDevice?.currentAllocatedSize ?? 0),
              thermal.rawValue, stats.loadedSplatCount, stats.drawnSplatCount, stoppedReason == nil ? 0 : 1)
        if footprint >= memoryLimitBytes { stopRun(.footprintLimit) }
        else if available < minAvailableBytes { stopRun(.availableMemoryBelow(bytes: minAvailableBytes)) }
        else if thermal == .serious || thermal == .critical { stopRun(.thermal(thermal)) }
    }

    private func stopRun(_ reason: StopReason) {
        guard stoppedReason == nil else { return }
        stoppedReason = reason
        resumed = false
        orbit?.end()
        view.pause()
        stopPolling()
        status = "paused: \(reason)"
        UIApplication.shared.isIdleTimerDisabled = false
        NSLog("SplatRunStopped: reason=%@", reason.description)
    }

    var resolutionText: String {
        let scale = view.window?.screen.nativeScale ?? UIScreen.main.nativeScale
        let width = Int((view.bounds.width * scale * CGFloat(view.renderScale)).rounded())
        let height = Int((view.bounds.height * scale * CGFloat(view.renderScale)).rounded())
        return "\(width)×\(height)"
    }

    var pipelineText: String {
        let policy = view.renderPolicy
        let sh = min(view.shDegree, view.maxShDegree, stats.loadedShDegree)
        let limit = policy.lodSplatLimit == 0 ? "all" : String(format: "%.1fM", Double(policy.lodSplatLimit) / 1_000_000)
        return "SH\(sh) \(policy.sortDepth)b \(limit)"
    }

    var thermalText: String {
        switch ProcessInfo.processInfo.thermalState {
        case .nominal: "Nominal"
        case .fair: "Fair"
        case .serious: "Serious"
        case .critical: "Critical"
        @unknown default: "Unknown"
        }
    }

    private final class Delegate: SplatViewDelegate {
        weak var session: SplatSession?
        init(session: SplatSession) { self.session = session }

        func splatView(_ view: SplatMetalView, worldReady splatCount: Int) {
            guard let session, session.stoppedReason == nil else { return }
            session.scenePrepared = false
            session.status = "\(splatCount) uploaded, preparing frame"
            NSLog("SplatPreparation: uploaded elapsed=%.3f", ProcessInfo.processInfo.systemUptime - session.preparationStarted)
        }

        func splatView(_ view: SplatMetalView, worldFrameReady splatCount: Int) {
            guard let session, session.stoppedReason == nil else { return }
            session.scenePrepared = true
            session.status = "\(splatCount) splats"
            let firstWorldFrame = session.firstLoadedTime == nil
            if firstWorldFrame { session.firstLoadedTime = ProcessInfo.processInfo.systemUptime }
            NSLog("SplatPreparation: first-frame-ready elapsed=%.3f", ProcessInfo.processInfo.systemUptime - session.preparationStarted)
            if session.resumed {
                session.orbit?.begin()
                NSLog("SplatPreparation: interaction-ready orbit=%d", session.orbit == nil ? 0 : 1)
            }
            if firstWorldFrame { session.beginScriptedRun() }
        }

        func splatView(_ view: SplatMetalView, worldFailed message: String) {
            session?.status = "world failed: \(message)"
            session?.loadingFailed = true
        }

        func splatViewColliderReady(_ view: SplatMetalView) {
            guard let session, !session.isScripted else { return }
            session.walking = true
        }

        func splatView(_ view: SplatMetalView, colliderFailed message: String) {
            session?.status = "collider failed: \(message)"
        }
    }
}

/// Why a run stopped, logged as `SplatRunStopped: reason=`.
enum StopReason: Equatable, CustomStringConvertible {
    case runInvalid, orbitRejected, orbitComplete, footprintLimit
    case memorySampleFailed(kern_return_t)
    case availableMemoryBelow(bytes: Int)
    case thermal(ProcessInfo.ThermalState)

    var description: String {
        switch self {
        case .runInvalid: "run-invalid"
        case .orbitRejected: "orbit-rejected"
        case .orbitComplete: "orbit-complete"
        case .footprintLimit: "footprint-limit"
        case .memorySampleFailed(let result): "memory-sample-failed-\(result)"
        case .availableMemoryBelow(let bytes): "available-memory-below-\(bytes >> 20)MiB"
        case .thermal(let state): "thermal-\(state.rawValue)"
        }
    }
}

struct SplatViewHost: UIViewRepresentable {
    let session: SplatSession

    func makeUIView(context: Context) -> SplatMetalView { session.view }
    func updateUIView(_ uiView: SplatMetalView, context: Context) {}
}

// Here, beside the view, so QualityTier.swift stays Foundation-only for the macOS test target.
extension QualityTier {
    func apply(to view: SplatMetalView) {
        view.renderScale = renderScale
        view.shDegree = shDegree
        var policy = view.renderPolicy
        policy.sortDepth = sortDepth
        policy.lodErrorPixels = lodErrorPixels
        policy.lodSplatLimit = lodSplatLimit
        view.renderPolicy = policy
    }
}
