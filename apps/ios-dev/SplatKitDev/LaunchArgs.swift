import Foundation

/// Command line switches, so a run can be scripted from `devicectl`:
/// `--world <file>` (a name in Documents or the bundle, or an absolute path),
/// `--tileset <file>` (a tileset.json), `--collider <file>`, `--residency <splats>`,
/// `--scale <f>`, `--sh <n>`, `--shdraw <n>`, `--budget <n>`, `--margin <deg>`,
/// `--linear`, `--gyro <0|1>`, `--pose x,y,z,yaw,pitch`, `--walk <m/s>`,
/// `--benchmark [seconds]`, `--capture <seconds>` (writes Documents/capture.png).
/// The default world is kitchen_500k.spz. ISS stays in Documents; use --world iss_10M.spz.
/// ISS runs a camera shot (--shot overview|orbit|detail|flyby|tour, default orbit) with +Y up.
/// The orbit shot circles at 135 m and 5 degrees/s from azimuth 20; --radius (at least 100),
/// --speed and --start override it.
/// Its starting pose is prepared during loading; motion starts only after the first
/// successful GPU frame. Loading stays covered while the renderer prepares that frame.
/// --orbit 0, --pose, --walk or --benchmark disable the automatic orbit and enable touch look.
/// No animated zoom. Gyro motion is opt-in with --gyro 1 on non-ISS worlds.
/// --metal-culling 1 opts into GPU-private scratch, footprint culling at 0.5 px,
/// and camera-depth sorting. Default 0 preserves the baseline; restart to change.
/// --min-pixel-radius <px> selects the experimental cutoff (default 0.5; try 1.0 or 1.2).
/// This is a source-footprint radius, not diameter, and has no effect without --metal-culling 1.
/// --depth-key-bits <16|32> selects linear camera-depth quantization and two radix
/// passes (16), or the unchanged four-pass ordering (32, default). Restart to change.
/// Quantization can change transparency ordering within a bin; no splats are removed.
/// --lod-error-pixels <px> sets the hierarchy refinement threshold (default 1; lower draws more).
/// --lod-splat-limit <n> caps the splats a frame selects, 0 for the loaded budget; detail thins evenly.
/// --tile-raster 1 sets the view's raster policy to hybrid: bounded 16x16 compute compositing,
/// with T <= 0.0001. It helps only where many large splats overlap; ISS orbits run slower.
/// Hardware fallback retains its existing 254/255 opacity coverage mask.
/// Dense tiles (>512 candidates) and tiles touched by large footprints (>16 tiles)
/// use hardware completion, never truncated lists. Scratch is capped at 128 MiB.
/// --quality <ultra|high|balanced|fast> picks the starting level (default high) unless --scale,
/// --shdraw, --depth-key-bits or --min-pixel-radius set the renderer directly.
/// --shot <overview|orbit|detail|flyby|tour> picks the ISS camera shot (default orbit).
/// --hud 0 hides the stats card and quality picker, for screen recordings.
/// --keep-awake 1 disables idle screen locking only while this dev view is active.
/// --resource-monitor 1 logs process footprint, remaining process allowance, Metal
/// allocation and thermal state at 2 Hz, and pauses on memory/thermal pressure.
/// --memory-limit-mib <MiB> lowers its conservative 2800 MiB process-footprint guard.
/// --run-seconds <seconds> enables monitoring and pauses that long after the first world frame.
/// These dev-only guards stop future frames, not allocations or GPU work in flight.
/// A GPU error stops submission until renderer recreation.
struct LaunchArgs {
    let values: [String: String]

    init(_ arguments: [String] = CommandLine.arguments) {
        var values: [String: String] = [:]
        var i = 1
        while i < arguments.count {
            let a = arguments[i]
            guard a.hasPrefix("--") else { i += 1; continue }
            let key = String(a.dropFirst(2))
            if i + 1 < arguments.count, !arguments[i + 1].hasPrefix("--") {
                values[key] = arguments[i + 1]
                i += 2
            } else {
                values[key] = ""
                i += 1
            }
        }
        self.values = values
    }

    func has(_ key: String) -> Bool { values[key] != nil }
    func string(_ key: String) -> String? { values[key].flatMap { $0.isEmpty ? nil : $0 } }
    func float(_ key: String) -> Float? { string(key).flatMap(Float.init) }
    func int(_ key: String) -> Int? { string(key).flatMap(Int.init) }
    func bool(_ key: String) -> Bool? {
        guard let v = values[key] else { return nil }
        return !(v == "0" || v == "false")
    }

    /// Resolves a world or collider argument: absolute paths as is, otherwise Documents
    /// first, then the bundle.
    static func resolve(_ name: String) -> URL? {
        if name.hasPrefix("/") { return URL(fileURLWithPath: name) }
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let inDocuments = documents.appendingPathComponent(name)
        if FileManager.default.fileExists(atPath: inDocuments.path) { return inDocuments }
        let url = URL(fileURLWithPath: name)
        return Bundle.main.url(forResource: url.deletingPathExtension().lastPathComponent,
                               withExtension: url.pathExtension)
    }
}
