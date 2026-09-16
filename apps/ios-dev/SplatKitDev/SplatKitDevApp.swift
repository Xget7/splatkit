import SwiftUI

@main
struct SplatKitDevApp: App {
    init() {
        // Configure the internal culling experiment and tile raster before SplatSession
        // creates its renderer. The threshold and sort key width are per-view policy now.
        let enabled = LaunchArgs().bool("metal-culling") ?? false
        setenv("SPLATKIT_METAL_CULLING_EXPERIMENT", enabled ? "1" : "0", 1)
        let tileRaster = LaunchArgs().bool("tile-raster") ?? false
        setenv("SPLATKIT_METAL_TILE_RASTER", tileRaster ? "1" : "0", 1)
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
                .ignoresSafeArea()
                .statusBarHidden()
        }
    }
}
