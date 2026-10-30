import Foundation
import Testing

/// One case of `scripts/benchmarks/run-samples.json`, shared with Android and the scripts.
struct RunSample: CustomTestStringConvertible, Sendable {
    let name: String
    let document: Data
    /// The key path iOS rejects the document with, nil when it must decode.
    let iosError: String?

    var testDescription: String { name }

    static let file = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent()
        .appendingPathComponent("../../../scripts/benchmarks/run-samples.json")
        .standardized

    static func load() throws -> [RunSample] {
        let cases = try JSONSerialization.jsonObject(with: Data(contentsOf: file)) as? [[String: Any]] ?? []
        return try cases.map { sample in
            RunSample(
                name: sample["name"] as? String ?? "",
                document: try JSONSerialization.data(withJSONObject: sample["run"] ?? NSNull(), options: [.fragmentsAllowed]),
                iosError: sample["ios_error"] as? String ?? sample["error"] as? String
            )
        }
    }
}

private func error(of decode: () throws -> RunFile) -> RunFileError? {
    do {
        _ = try decode()
        return nil
    } catch let error as RunFileError {
        return error
    } catch {
        Issue.record("not a RunFileError: \(error)")
        return nil
    }
}

private func runArguments(_ json: String) -> [String] {
    ["/var/containers/Bundle/Application/SplatKitDev.app/SplatKitDev", RunFile.argument, json]
}

@Suite struct RunFileSamplesTests {
    @Test func samplesExist() throws {
        #expect(try RunSample.load().count > 10)
    }

    @Test(arguments: try RunSample.load())
    func sample(_ sample: RunSample) throws {
        if let expected = sample.iosError {
            let failure = error { try RunFile(json: sample.document) }
            #expect(failure?.keyPath == expected, "\(failure.map(String.init(describing:)) ?? "decoded")")
        } else {
            #expect(throws: Never.self) { try RunFile(json: sample.document) }
        }
    }
}

@Suite struct RunFileDecodingTests {
    @Test func decodesEveryField() throws {
        let run = try RunFile(json: Data("""
            {"schema_version": 1,
             "world": {"file": "house.spz", "source_frame": "RDF", "collider": "house.glb"},
             "renderer": {"quality": "ultra", "budget": 0, "subpixel": 0.5, "lod_error_pixels": 0.5, "lod_splat_limit": 1000000},
             "pose": [0, 1.2, 3, -1.57, 0.1],
             "run": {"seconds": 12.5, "orbit": {"anchor": [0, 1, 0], "degrees": -120}, "capture_at": 4, "monitor": true}}
            """.utf8))
        #expect(run.world == RunFile.World(file: "house.spz", sourceFrame: "RDF", collider: "house.glb"))
        #expect(run.world?.streamsTiles == false)
        #expect(run.renderer == RunFile.Renderer(quality: .ultra, budget: 0, subpixel: 0.5, lodErrorPixels: 0.5, lodSplatLimit: 1_000_000))
        #expect(run.pose == RunFile.Pose(x: 0, y: 1.2, z: 3, yaw: -1.57, pitch: 0.1))
        #expect(run.run == RunFile.Run(seconds: 12.5, orbit: RunFile.Orbit(anchor: SIMD3(0, 1, 0), degrees: -120), captureAt: 4, monitor: true))
    }

    @Test func aJsonWorldStreamsTiles() throws {
        let run = try RunFile(json: Data(#"{"schema_version": 1, "world": {"file": "city/tileset.JSON"}}"#.utf8))
        #expect(run.world?.streamsTiles == true)
    }

    @Test func errorsNameTheKeyAndTheProblem() {
        let misspelt = error { try RunFile(json: Data(#"{"schema_version": 1, "renderer": {"lod_eror_pixels": 0.5}}"#.utf8)) }
        #expect(misspelt?.description == "renderer.lod_eror_pixels: unknown key")
        let text = error { try RunFile(json: Data(#"{"schema_version": 1, "renderer": {"subpixel": "1.o"}}"#.utf8)) }
        #expect(text?.description == "renderer.subpixel: expected a number")
        let missing = error { try RunFile(json: Data(#"{"schema_version": 1, "run": {"orbit": {"degrees": 90}, "seconds": 3}}"#.utf8)) }
        #expect(missing?.description == "run.orbit.anchor: required")
    }

    @Test func rejectsANumberForABoolean() {
        let failure = error { try RunFile(json: Data(#"{"schema_version": 1, "run": {"seconds": 3, "monitor": 1}}"#.utf8)) }
        #expect(failure?.keyPath == "run.monitor")
    }

    @Test func rejectsABooleanForANumber() {
        let failure = error { try RunFile(json: Data(#"{"schema_version": 1, "renderer": {"subpixel": true}}"#.utf8)) }
        #expect(failure?.keyPath == "renderer.subpixel")
    }

    @Test func rejectsNullForAValue() {
        let failure = error { try RunFile(json: Data(#"{"schema_version": 1, "renderer": {"budget": null}}"#.utf8)) }
        #expect(failure?.keyPath == "renderer.budget")
    }

    @Test func rejectsAnEmptyWorldFile() {
        let failure = error { try RunFile(json: Data(#"{"schema_version": 1, "world": {"file": ""}}"#.utf8)) }
        #expect(failure?.keyPath == "world.file")
    }

    @Test func rejectsASectionThatIsNotAnObject() {
        let failure = error { try RunFile(json: Data(#"{"schema_version": 1, "run": 30}"#.utf8)) }
        #expect(failure?.keyPath == "run")
    }

    @Test func rejectsADocumentThatIsNotAnObject() {
        #expect(error { try RunFile(json: Data("[1]".utf8)) } != nil)
        #expect(error { try RunFile(json: Data("{".utf8)) } != nil)
    }

    @Test func rejectsANegativeCaptureTime() {
        let failure = error { try RunFile(json: Data(#"{"schema_version": 1, "run": {"seconds": 3, "capture_at": -1}}"#.utf8)) }
        #expect(failure?.keyPath == "run.capture_at")
    }

    @Test func rejectsASplatLimitPastThirtyTwoBits() {
        let failure = error { try RunFile(json: Data(#"{"schema_version": 1, "renderer": {"lod_splat_limit": 4294967296}}"#.utf8)) }
        #expect(failure?.keyPath == "renderer.lod_splat_limit")
    }

    @Test func encodesTheAcceptedDocumentCompactly() throws {
        let run = try RunFile(json: Data(#"{"schema_version": 1, "renderer": {"quality": "fast"}, "run": {"seconds": 2}}"#.utf8))
        #expect(run.compactJSON == #"{"renderer":{"quality":"fast"},"run":{"seconds":2},"schema_version":1}"#)
        #expect(try RunFile(json: Data(run.compactJSON.utf8)) == run)
    }
}

@Suite struct RunFileArgumentTests {
    @Test func noRunArgumentIsInteractive() throws {
        let run = try RunFile(arguments: ["/path/SplatKitDev"])
        #expect(run == RunFile())
        #expect(run.run == nil)
    }

    @Test func systemArgumentsAreIgnored() throws {
        let run = try RunFile(arguments: ["/path/SplatKitDev", "-NSDocumentRevisionsDebugMode", "YES"])
        #expect(run == RunFile())
    }

    @Test func runArgumentCarriesTheDocument() throws {
        let run = try RunFile(arguments: runArguments(#"{"schema_version": 1, "run": {"seconds": 30}}"#))
        #expect(run.run?.seconds == 30)
    }

    @Test func anOldFlagNamesItself() {
        let failure = error { try RunFile(arguments: ["/path/SplatKitDev", "--world", "scene.lodsplat"]) }
        #expect(failure == .unknownArgument("--world"))
        #expect(failure?.description == "unknown argument --world; pass settings in --run")
    }

    @Test func anOldFlagNextToARunFails() {
        let failure = error { try RunFile(arguments: runArguments(#"{"schema_version": 1}"#) + ["--hud", "0"]) }
        #expect(failure == .unknownArgument("--hud"))
    }

    @Test func runWithoutAValueFails() {
        #expect(error { try RunFile(arguments: ["/path/SplatKitDev", RunFile.argument]) } == .missingDocument)
    }

    @Test func runTwiceFails() {
        let json = #"{"schema_version": 1}"#
        #expect(error { try RunFile(arguments: runArguments(json) + [RunFile.argument, json]) } == .repeatedArgument)
    }

    @Test func anInvalidRunDocumentFails() {
        let failure = error { try RunFile(arguments: runArguments(#"{"schema_version": 1, "hud": false}"#)) }
        #expect(failure?.keyPath == "hud")
    }
}
