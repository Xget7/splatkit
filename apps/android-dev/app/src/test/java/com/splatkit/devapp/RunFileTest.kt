package com.splatkit.devapp

import com.splatkit.CameraPose
import com.splatkit.CoordinateFrame
import com.splatkit.RenderQuality
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import java.io.File

private const val SAMPLES = "scripts/benchmarks/run-samples.json"
private const val SCHEMA = "scripts/benchmarks/run.schema.json"

/** Case keys run-samples.json may use; another one is a platform rule this test would miss. */
private val SAMPLE_KEYS = setOf("name", "run", "error", "android_error", "ios_error")

/** Finds [path] from the working directory up, so the test runs from Gradle or the repo root. */
private fun repoFile(path: String): File =
    generateSequence(File("").absoluteFile) { it.parentFile }
        .map { File(it, path) }
        .firstOrNull { it.isFile }
        ?: error("$path not found above ${File("").absolutePath}")

private fun readJson(path: String) = Json.parseToJsonElement(repoFile(path).readText())

private fun rejectionPath(json: String): String? =
    try {
        RunFile.decode(json)
        null
    } catch (e: RunFileException) {
        e.path
    }

class RunFileTest {
    @Test fun everySharedSampleDecodesOrFailsAtItsKeyPath() {
        val cases = readJson(SAMPLES).jsonArray.map { it.jsonObject }
        assertTrue(cases.isNotEmpty())
        val mismatches = cases.mapNotNull { case ->
            val name = case.getValue("name").jsonPrimitive.content
            val unknown = case.keys - SAMPLE_KEYS
            if (unknown.isNotEmpty()) return@mapNotNull "$name: unknown case keys $unknown"
            val expected = (case["android_error"] ?: case["error"])?.jsonPrimitive?.content
            val actual = rejectionPath(case.getValue("run").toString())
            if (actual == expected) null else "$name: expected ${expected ?: "accepted"}, got ${actual ?: "accepted"}"
        }
        assertTrue(mismatches.joinToString("\n"), mismatches.isEmpty())
    }

    @Test fun androidQualityNamesAreTheSdkPresetNames() {
        val schemaNames = readJson(SCHEMA).jsonObject.getValue("properties").jsonObject
            .getValue("renderer").jsonObject.getValue("properties").jsonObject
            .getValue("quality").jsonObject.getValue("enum").jsonArray.map { it.jsonPrimitive.content }
        schemaNames.forEach { name ->
            val run = """{"schema_version": 1, "renderer": {"quality": "$name"}}"""
            val preset = RenderQuality.named(name)
            if (preset == null) {
                assertEquals(name, "renderer.quality", rejectionPath(run))
            } else {
                assertEquals(name, preset, RunFile.decode(run).renderer.quality)
            }
        }
    }

    @Test fun theReferenceBenchmarkDecodesToTypedSettings() {
        val run = RunFile.decode(
            """{"schema_version": 1, "world": {"file": "scene.lodsplat"},
               "renderer": {"quality": "high", "budget": 1800000, "subpixel": 1.0},
               "pose": [-7.25, 1.6, 5.25, 0, 0], "run": {"seconds": 30}}""",
        )
        assertEquals(RunWorld(file = "scene.lodsplat"), run.world)
        assertEquals(RunRenderer(quality = RenderQuality.HIGH, budget = 1_800_000, subpixel = 1f), run.renderer)
        assertEquals(CameraPose(-7.25f, 1.6f, 5.25f, 0f, 0f), run.pose)
        assertEquals(RunSchedule(seconds = 30f), run.run)
    }

    @Test fun everyAndroidFieldDecodes() {
        val run = RunFile.decode(
            """{"schema_version": 1,
               "world": {"file": "/sdcard/house.json", "source_frame": "RDF", "collider": "house.glb"},
               "renderer": {"lod_error_pixels": 0.5, "lod_splat_limit": 1000000},
               "run": {"seconds": 12.5}}""",
        )
        assertEquals(RunWorld("/sdcard/house.json", CoordinateFrame.RDF, "house.glb"), run.world)
        assertEquals(RunRenderer(lodErrorPixels = 0.5f, lodSplatLimit = 1_000_000), run.renderer)
        assertNull(run.pose)
        assertEquals(RunSchedule(12.5f), run.run)
    }

    @Test fun anEmptyRunHasNoSections() {
        assertEquals(RunFile(), RunFile.decode("""{"schema_version": 1}"""))
    }

    @Test fun integersMayBeWrittenWithAZeroFraction() {
        assertEquals(1_000_000, RunFile.decode("""{"schema_version": 1.0, "renderer": {"budget": 1e6}}""").renderer.budget)
    }

    @Test fun wrongTypesAreRejectedAtTheirKey() {
        assertEquals("run.seconds", rejectionPath("""{"schema_version": 1, "run": {"seconds": true}}"""))
        assertEquals("run.seconds", rejectionPath("""{"schema_version": 1, "run": {"seconds": null}}"""))
        assertEquals("world.file", rejectionPath("""{"schema_version": 1, "world": {"file": 3}}"""))
        assertEquals("world", rejectionPath("""{"schema_version": 1, "world": "house.spz"}"""))
        assertEquals("pose.2", rejectionPath("""{"schema_version": 1, "pose": [0, 1, "2", 0, 0]}"""))
        assertEquals("renderer.budget", rejectionPath("""{"schema_version": 1, "renderer": {"budget": 3000000000}}"""))
        assertEquals("renderer.subpixel", rejectionPath("""{"schema_version": 1, "renderer": {"subpixel": 1f}}"""))
        assertEquals("renderer.subpixel", rejectionPath("""{"schema_version": 1, "renderer": {"subpixel": NaN}}"""))
        assertEquals("renderer.quality", rejectionPath("""{"schema_version": 1, "renderer": {"quality": "HIGH"}}"""))
    }

    @Test fun anIosOnlyFieldNamesThePlatform() {
        try {
            RunFile.decode("""{"schema_version": 1, "run": {"seconds": 1, "monitor": false}}""")
            fail("monitor accepted")
        } catch (e: RunFileException) {
            assertEquals("run.monitor: not supported on Android", e.message)
        }
    }

    @Test fun aSourceFrameMustNameThreeDistinctAxes() {
        assertEquals("world.source_frame", rejectionPath(
            """{"schema_version": 1, "world": {"file": "a.spz", "source_frame": "rru"}}""",
        ))
    }

    @Test fun aDocumentThatIsNotAJsonObjectIsRejected() {
        assertEquals("", rejectionPath("""{"schema_version": 1"""))
        assertEquals("", rejectionPath("""[1]"""))
    }
}

class LaunchTest {
    @Test fun noExtrasOpenTheDefaultWorld() {
        assertEquals(Launch.Default, Launch.fromExtras(emptySet(), null))
    }

    @Test fun anotherExtraIsRejectedByName() {
        assertEquals(
            Launch.Invalid("unknown extra world; pass settings in the run extra"),
            Launch.fromExtras(setOf("world"), null),
        )
        assertEquals(
            Launch.Invalid("unknown extra quality; pass settings in the run extra"),
            Launch.fromExtras(setOf(RUN_EXTRA, "quality"), """{"schema_version": 1}"""),
        )
    }

    @Test fun namespacedExtrasFromTheSystemOrALauncherAreIgnored() {
        assertEquals(Launch.Default, Launch.fromExtras(setOf("android.intent.extra.REFERRER"), null))
    }

    @Test fun aRunExtraThatIsNotAStringIsRejected() {
        assertTrue(Launch.fromExtras(setOf(RUN_EXTRA), null) is Launch.Invalid)
    }

    @Test fun anInvalidRunCarriesTheKeyPath() {
        assertEquals(
            Launch.Invalid("renderer.lod_eror_pixels: unknown key"),
            Launch.fromExtras(setOf(RUN_EXTRA), """{"schema_version": 1, "renderer": {"lod_eror_pixels": 0.5}}"""),
        )
    }

    @Test fun anAcceptedRunKeepsTheCompactDocument() {
        val launch = Launch.fromExtras(setOf(RUN_EXTRA), """{ "schema_version" : 1, "run": { "seconds": 30 } }""")
        assertEquals(
            Launch.Run(RunFile(run = RunSchedule(30f)), """{"schema_version":1,"run":{"seconds":30}}"""),
            launch,
        )
    }
}
