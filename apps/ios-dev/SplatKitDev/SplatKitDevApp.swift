import SwiftUI

@main
struct SplatKitDevApp: App {
    init() {
        // The renderer reads this when it is created, so set it before SplatSession makes one.
        // The threshold, sort key width and raster strategy are per-view policy.
        let enabled = LaunchArgs().bool("metal-culling") ?? false
        setenv("SPLATKIT_METAL_CULLING_EXPERIMENT", enabled ? "1" : "0", 1)
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
                .statusBarHidden()
        }
    }
}
