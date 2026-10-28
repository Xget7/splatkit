import Foundation

/// What the dev app loads and how it runs it, passed whole as `--run '<json>'`. The format is
/// `scripts/benchmarks/run.schema.json`; decoding is strict, so a misspelt key or a bad value
/// stops the launch. Without `--run` the app opens the default world for interactive use.
struct RunFile: Equatable {
    static let argument = "--run"
    static let schemaVersion = 1

    var world: World?
    var renderer: Renderer?
    var pose: Pose?
    /// Present for a scripted run.
    var run: Run?

    struct World: Equatable {
        /// An absolute path, or a name in Documents, then the bundle.
        var file: String
        /// Three axis letters, such as rdf; nil for the format's own frame.
        var sourceFrame: String?
        var collider: String?

        static let tilesetExtension = ".json"

        var streamsTiles: Bool { file.lowercased().hasSuffix(Self.tilesetExtension) }
    }

    /// The quality level first, then each field given.
    struct Renderer: Equatable {
        var quality: QualityTier?
        var budget: Int?
        var subpixel: Float?
        var lodErrorPixels: Float?
        var lodSplatLimit: UInt32?
    }

    /// Metres, then radians.
    struct Pose: Equatable {
        var x: Float
        var y: Float
        var z: Float
        var yaw: Float
        var pitch: Float
    }

    struct Run: Equatable {
        var seconds: TimeInterval
        var orbit: Orbit?
        /// Seconds after the first world frame.
        var captureAt: TimeInterval?
        var monitor: Bool?
    }

    struct Orbit: Equatable {
        var anchor: SIMD3<Float>
        /// Azimuth sweep; positive turns toward +X.
        var degrees: Float
    }
}

enum RunFileError: Error, Equatable, CustomStringConvertible {
    /// A launch argument other than `--run`, such as one of the flags the run file replaced.
    case unknownArgument(String)
    case missingDocument
    case repeatedArgument
    case notJSON
    case invalid(keyPath: String, problem: Problem)

    enum Problem: Equatable, CustomStringConvertible {
        case unknownKey
        case missing
        case expected(Kind)
        case oneOf([String])
        case unsupportedVersion
        case atLeast(Double)
        case greaterThan(Double)
        case atMost(Double)
        case nonZero
        case nonEmpty
        case axisLetters
        case outOfRange

        var description: String {
            switch self {
            case .unknownKey: "unknown key"
            case .missing: "required"
            case .expected(let kind): "expected \(kind)"
            case .oneOf(let values): "expected one of \(values.joined(separator: ", "))"
            case .unsupportedVersion: "expected \(RunFile.schemaVersion)"
            case .atLeast(let bound): "must be at least \(Self.format(bound))"
            case .greaterThan(let bound): "must be greater than \(Self.format(bound))"
            case .atMost(let bound): "must be at most \(Self.format(bound))"
            case .nonZero: "must not be 0"
            case .nonEmpty: "must not be empty"
            case .axisLetters: "expected one letter of each of \(RunFile.World.axes.joined(separator: ", "))"
            case .outOfRange: "out of range"
            }
        }

        private static func format(_ bound: Double) -> String {
            bound.rounded() == bound ? String(Int64(bound)) : String(bound)
        }
    }

    enum Kind: Equatable, CustomStringConvertible {
        case object, number, integer, string, boolean
        case numbers(Int)

        var description: String {
            switch self {
            case .object: "an object"
            case .number: "a number"
            case .integer: "an integer"
            case .string: "a string"
            case .boolean: "true or false"
            case .numbers(let count): "\(count) numbers"
            }
        }
    }

    /// Dotted, such as `renderer.subpixel`; nil for an error outside the document.
    var keyPath: String? {
        if case .invalid(let keyPath, _) = self { keyPath } else { nil }
    }

    var description: String {
        switch self {
        case .unknownArgument(let argument): "unknown argument \(argument); pass settings in \(RunFile.argument)"
        case .missingDocument: "\(RunFile.argument) needs a JSON document"
        case .repeatedArgument: "\(RunFile.argument) given more than once"
        case .notJSON: "\(RunFile.argument): not valid JSON"
        case .invalid(let keyPath, let problem): "\(keyPath.isEmpty ? RunFile.argument : keyPath): \(problem)"
        }
    }

    static func keyPath(_ codingPath: [CodingKey]) -> String {
        codingPath.map(\.stringValue).joined(separator: ".")
    }
}

extension RunFile {
    /// The run file given with `--run`, or the interactive defaults without one. Any other
    /// argument starting with `--` is an error, which stops scripts written for the old launch
    /// flags; arguments the system adds start with one dash or none.
    init(arguments: [String]) throws {
        var document: String?
        var rest = arguments[...]
        while let argument = rest.popFirst() {
            guard argument.hasPrefix("--") else { continue }
            guard argument == Self.argument else { throw RunFileError.unknownArgument(argument) }
            guard document == nil else { throw RunFileError.repeatedArgument }
            guard let value = rest.popFirst(), !value.hasPrefix("--") else { throw RunFileError.missingDocument }
            document = value
        }
        guard let document else {
            self.init()
            return
        }
        try self.init(json: Data(document.utf8))
    }

    init(json: Data) throws {
        do {
            self = try JSONDecoder().decode(RunFile.self, from: json)
        } catch is DecodingError {
            // Every key is read through the checks below, so only unparsable text lands here.
            throw RunFileError.notJSON
        }
    }

    /// The document as decoded, for the launch log.
    var compactJSON: String {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys, .withoutEscapingSlashes]
        // Decoded values are finite, so encoding cannot fail.
        return String(decoding: try! encoder.encode(self), as: UTF8.self)
    }
}

extension RunFile: Codable {
    private enum CodingKeys: String, CodingKey {
        case schemaVersion = "schema_version", world, renderer, pose, run
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.strictContainer(keyedBy: CodingKeys.self)
        let version = try container.required(.schemaVersion, container.integer)
        try container.check(version == Self.schemaVersion, .schemaVersion, .unsupportedVersion)
        world = try container.section(World.self, .world)
        renderer = try container.section(Renderer.self, .renderer)
        pose = try container.section(Pose.self, .pose)
        run = try container.section(Run.self, .run)
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(Self.schemaVersion, forKey: .schemaVersion)
        try container.encodeIfPresent(world, forKey: .world)
        try container.encodeIfPresent(renderer, forKey: .renderer)
        try container.encodeIfPresent(pose, forKey: .pose)
        try container.encodeIfPresent(run, forKey: .run)
    }
}

extension RunFile.World: Codable {
    /// A source frame names each axis once, by either of its two directions.
    static let axes = ["rl", "ud", "bf"]

    private enum CodingKeys: String, CodingKey {
        case file, sourceFrame = "source_frame", collider
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.strictContainer(keyedBy: CodingKeys.self)
        file = try container.required(.file, container.string)
        try container.check(!file.isEmpty, .file, .nonEmpty)
        sourceFrame = try container.string(.sourceFrame)
        if let sourceFrame {
            let letters = sourceFrame.lowercased()
            try container.check(letters.count == Self.axes.count && Self.axes.allSatisfy { axis in letters.filter(axis.contains).count == 1 },
                                .sourceFrame, .axisLetters)
        }
        collider = try container.string(.collider)
        if let collider { try container.check(!collider.isEmpty, .collider, .nonEmpty) }
    }
}

extension RunFile.Renderer: Codable {
    private enum CodingKeys: String, CodingKey {
        case quality, budget, subpixel, lodErrorPixels = "lod_error_pixels", lodSplatLimit = "lod_splat_limit"
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.strictContainer(keyedBy: CodingKeys.self)
        if let name = try container.string(.quality) {
            quality = QualityTier(rawValue: name)
            try container.check(quality != nil, .quality, .oneOf(QualityTier.allCases.map(\.rawValue)))
        }
        budget = try container.integer(.budget)
        if let budget {
            try container.check(budget >= 0, .budget, .atLeast(0))
            try container.check(budget <= Int32.max, .budget, .atMost(Double(Int32.max)))
        }
        subpixel = try container.float(.subpixel)
        if let subpixel { try container.check(subpixel >= 0, .subpixel, .atLeast(0)) }
        lodErrorPixels = try container.float(.lodErrorPixels)
        if let lodErrorPixels { try container.check(lodErrorPixels > 0, .lodErrorPixels, .greaterThan(0)) }
        if let limit = try container.integer(.lodSplatLimit) {
            try container.check(limit >= 0, .lodSplatLimit, .atLeast(0))
            try container.check(limit <= Int32.max, .lodSplatLimit, .atMost(Double(Int32.max)))
            lodSplatLimit = UInt32(limit)
        }
    }
}

extension RunFile.Pose: Codable {
    private static let count = 5

    init(from decoder: Decoder) throws {
        var values: [Float] = []
        do {
            var container = try decoder.unkeyedContainer()
            while !container.isAtEnd { values.append(try container.decode(Float.self)) }
        } catch is DecodingError {
            values = []
        }
        guard values.count == Self.count, values.allSatisfy(\.isFinite) else {
            throw RunFileError.invalid(keyPath: RunFileError.keyPath(decoder.codingPath), problem: .expected(.numbers(Self.count)))
        }
        self.init(x: values[0], y: values[1], z: values[2], yaw: values[3], pitch: values[4])
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.unkeyedContainer()
        try container.encode(contentsOf: [x, y, z, yaw, pitch])
    }
}

extension RunFile.Run: Codable {
    private enum CodingKeys: String, CodingKey {
        case seconds, orbit, captureAt = "capture_at", monitor
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.strictContainer(keyedBy: CodingKeys.self)
        seconds = try container.required(.seconds, container.number)
        try container.check(seconds > 0, .seconds, .greaterThan(0))
        orbit = try container.section(RunFile.Orbit.self, .orbit)
        captureAt = try container.number(.captureAt)
        if let captureAt { try container.check(captureAt >= 0, .captureAt, .atLeast(0)) }
        monitor = try container.boolean(.monitor)
    }
}

extension RunFile.Orbit: Codable {
    private static let anchorCount = 3

    private enum CodingKeys: String, CodingKey {
        case anchor, degrees
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.strictContainer(keyedBy: CodingKeys.self)
        let anchor = try container.required(.anchor) { try container.numbers($0, count: Self.anchorCount) }
        self.anchor = SIMD3(anchor[0], anchor[1], anchor[2])
        degrees = try container.required(.degrees, container.float)
        try container.check(degrees != 0, .degrees, .nonZero)
    }
}

private struct AnyKey: CodingKey {
    let stringValue: String
    let intValue: Int? = nil

    init(stringValue: String) { self.stringValue = stringValue }
    init?(intValue: Int) { nil }
}

private extension Decoder {
    /// A keyed container that rejects any key `Key` does not name.
    func strictContainer<Key: CodingKey>(keyedBy type: Key.Type) throws -> KeyedDecodingContainer<Key> {
        let all: KeyedDecodingContainer<AnyKey>
        do {
            all = try container(keyedBy: AnyKey.self)
        } catch is DecodingError {
            throw RunFileError.invalid(keyPath: RunFileError.keyPath(codingPath), problem: .expected(.object))
        }
        if let unknown = all.allKeys.map(\.stringValue).sorted().first(where: { Key(stringValue: $0) == nil }) {
            throw RunFileError.invalid(keyPath: RunFileError.keyPath(codingPath + [AnyKey(stringValue: unknown)]),
                                       problem: .unknownKey)
        }
        return try container(keyedBy: type)
    }
}

/// Typed reads that report the key's path. Each returns nil for an absent key and rejects a
/// present one of the wrong type, null included.
private extension KeyedDecodingContainer {
    func failure(_ key: Key, _ problem: RunFileError.Problem) -> RunFileError {
        .invalid(keyPath: RunFileError.keyPath(codingPath + [key]), problem: problem)
    }

    func check(_ condition: Bool, _ key: Key, _ problem: RunFileError.Problem) throws {
        if !condition { throw failure(key, problem) }
    }

    func required<Value>(_ key: Key, _ read: (Key) throws -> Value?) throws -> Value {
        guard let value = try read(key) else { throw failure(key, .missing) }
        return value
    }

    func read<Value: Decodable>(_ type: Value.Type, _ key: Key, as kind: RunFileError.Kind) throws -> Value? {
        guard contains(key) else { return nil }
        do {
            return try decode(type, forKey: key)
        } catch is DecodingError {
            throw failure(key, .expected(kind))
        }
    }

    func number(_ key: Key) throws -> Double? {
        try read(Double.self, key, as: .number)
    }

    func float(_ key: Key) throws -> Float? {
        guard let value = try number(key) else { return nil }
        let float = Float(value)
        try check(float.isFinite, key, .outOfRange)
        return float
    }

    func integer(_ key: Key) throws -> Int? {
        guard let value = try read(Double.self, key, as: .integer) else { return nil }
        guard let integer = Int(exactly: value) else { throw failure(key, .expected(.integer)) }
        return integer
    }

    func string(_ key: Key) throws -> String? {
        try read(String.self, key, as: .string)
    }

    func boolean(_ key: Key) throws -> Bool? {
        try read(Bool.self, key, as: .boolean)
    }

    func numbers(_ key: Key, count: Int) throws -> [Float]? {
        guard let values = try read([Float].self, key, as: .numbers(count)) else { return nil }
        try check(values.count == count && values.allSatisfy(\.isFinite), key, .expected(.numbers(count)))
        return values
    }

    /// A nested object; its own reads report their paths.
    func section<Value: Decodable>(_ type: Value.Type, _ key: Key) throws -> Value? {
        guard contains(key) else { return nil }
        return try decode(type, forKey: key)
    }
}
