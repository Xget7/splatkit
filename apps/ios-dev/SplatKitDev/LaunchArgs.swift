import Foundation

/// Command line switches, so a run can be scripted from `devicectl`:
/// `--world <file>` (a name in Documents or the bundle, or an absolute path),
/// `--tileset <file>` (a tileset.json), `--collider <file>`, `--residency <splats>`,
/// `--scale <f>`, `--sh <n>`, `--shdraw <n>`, `--budget <n>`, `--margin <deg>`,
/// `--linear`, `--gyro <0|1>`, `--pose x,y,z,yaw,pitch`, `--walk <m/s>`,
/// `--benchmark [seconds]`, `--capture <seconds>` (writes Documents/capture.png).
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
