import Foundation

/// Where the dev app finds worlds and colliders and writes captures.
enum AppFiles {
    static let captureName = "capture.png"

    static var documents: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
    }

    /// An absolute path as is, otherwise a name in Documents first, then the bundle.
    static func resolve(_ name: String) -> URL? {
        if name.hasPrefix("/") { return URL(fileURLWithPath: name) }
        let inDocuments = documents.appendingPathComponent(name)
        if FileManager.default.fileExists(atPath: inDocuments.path) { return inDocuments }
        let url = URL(fileURLWithPath: name)
        return Bundle.main.url(forResource: url.deletingPathExtension().lastPathComponent,
                               withExtension: url.pathExtension)
    }
}
