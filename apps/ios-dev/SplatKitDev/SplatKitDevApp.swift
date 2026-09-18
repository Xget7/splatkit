import SwiftUI

@main
struct SplatKitDevApp: App {
    init() {
        // Configure the internal culling experiment before SplatSession creates its renderer.
        // The threshold, sort key width and raster strategy are per-view policy now.
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
