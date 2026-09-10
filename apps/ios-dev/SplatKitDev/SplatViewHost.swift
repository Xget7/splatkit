import SwiftUI
import UIKit

/// The one SplatMetalView of the app, shared with SwiftUI through an observable owner so
/// the HUD can read stats and push settings without recreating the view.
final class SplatSession: ObservableObject {
    let view = SplatMetalView()
    @Published var stats = SplatStats()
    @Published var status = "loading"
    @Published var walking = false
    @Published var motion = false
    @Published var gpu = ""
    private var timer: Timer?
    private var delegateBox: Delegate?

    init() {
        let delegate = Delegate(session: self)
        delegateBox = delegate
        view.delegate = delegate
        gpu = view.gpuDescription
    }

    func startPolling() {
        timer?.invalidate()
        timer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            guard let self else { return }
            self.stats = self.view.readStats()
            self.motion = self.view.isMotionEnabled
        }
    }

    func stopPolling() {
        timer?.invalidate()
        timer = nil
    }

    private final class Delegate: SplatViewDelegate {
        weak var session: SplatSession?
        init(session: SplatSession) { self.session = session }

        func splatView(_ view: SplatMetalView, worldReady splatCount: Int) {
            session?.status = "\(splatCount) splats"
        }

        func splatView(_ view: SplatMetalView, worldFailed message: String) {
            session?.status = "world failed: \(message)"
        }

        func splatViewColliderReady(_ view: SplatMetalView) {
            session?.walking = true
        }

        func splatView(_ view: SplatMetalView, colliderFailed message: String) {
            session?.status = "collider failed: \(message)"
        }
    }
}

struct SplatViewHost: UIViewRepresentable {
    let session: SplatSession

    func makeUIView(context: Context) -> SplatMetalView { session.view }
    func updateUIView(_ uiView: SplatMetalView, context: Context) {}
}
