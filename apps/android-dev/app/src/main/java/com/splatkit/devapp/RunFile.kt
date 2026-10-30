package com.splatkit.devapp

import com.splatkit.CameraPose
import com.splatkit.CoordinateFrame
import com.splatkit.RenderQuality
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import java.math.BigDecimal

/** The intent extra holding the whole run file as JSON. */
const val RUN_EXTRA = "run"


private const val SCHEMA_VERSION = 1
private const val POSE_LENGTH = 5

private object Key {
    const val SCHEMA_VERSION = "schema_version"
    const val WORLD = "world"
    const val RENDERER = "renderer"
    const val POSE = "pose"
    const val RUN = "run"
    const val FILE = "file"
    const val SOURCE_FRAME = "source_frame"
    const val COLLIDER = "collider"
    const val QUALITY = "quality"
    const val BUDGET = "budget"
    const val SUBPIXEL = "subpixel"
    const val LOD_ERROR_PIXELS = "lod_error_pixels"
    const val LOD_SPLAT_LIMIT = "lod_splat_limit"
    const val SECONDS = "seconds"
    const val ORBIT = "orbit"
    const val CAPTURE_AT = "capture_at"
    const val MONITOR = "monitor"
    const val ANCHOR = "anchor"
    const val DEGREES = "degrees"
}

/** RFC 8259 number; the tree parser also passes bare tokens such as `1f` through. */
private val JSON_NUMBER = Regex("^-?(0|[1-9][0-9]*)(\\.[0-9]+)?([eE][+-]?[0-9]+)?$")

/** Thrown for a run file that breaks `scripts/benchmarks/run.schema.json` or names an iOS-only field. */
class RunFileException(
    /** Dotted key path of the offending value; empty for the document itself. */
    val path: String,
    reason: String,
) : IllegalArgumentException(if (path.isEmpty()) reason else "$path: $reason")

/** A decoded run file. Absent sections take the app's defaults: bundled world, interactive. */
data class RunFile(
    val world: RunWorld? = null,
    val renderer: RunRenderer = RunRenderer(),
    val pose: CameraPose? = null,
    /** Non-null makes a scripted run. */
    val run: RunSchedule? = null,
) {
    companion object {
        /** Throws [RunFileException]. */
        fun decode(json: String): RunFile = decode(parse(json))

        internal fun parse(json: String): JsonElement =
            try {
                Json.parseToJsonElement(json)
            } catch (e: SerializationException) {
                throw RunFileException("", "not JSON: ${e.message}")
            }

        internal fun decode(document: JsonElement): RunFile {
            if (document !is JsonObject) throw RunFileException("", "the run file must be a JSON object")
            val root = document.fields(
                "",
                known = setOf(Key.SCHEMA_VERSION, Key.WORLD, Key.RENDERER, Key.POSE, Key.RUN),
                required = setOf(Key.SCHEMA_VERSION),
            )
            val version = root.getValue(Key.SCHEMA_VERSION)
            if (version.integer(Key.SCHEMA_VERSION) != SCHEMA_VERSION) {
                fail(Key.SCHEMA_VERSION, "must be $SCHEMA_VERSION")
            }
            return RunFile(
                world = root[Key.WORLD]?.let(::world),
                renderer = root[Key.RENDERER]?.let(::renderer) ?: RunRenderer(),
                pose = root[Key.POSE]?.let(::pose),
                run = root[Key.RUN]?.let(::schedule),
            )
        }
    }
}

data class RunWorld(
    /** Absolute, or relative to the external files dir. A `.json` file is a tiled world. */
    val file: String,
    /** Null reads the file's own frame. */
    val sourceFrame: CoordinateFrame? = null,
    val collider: String? = null,
)

/** Null fields keep the quality level's value. */
data class RunRenderer(
    /** Null is high. */
    val quality: RenderQuality? = null,
    val budget: Int? = null,
    val subpixel: Float? = null,
    val lodErrorPixels: Float? = null,
    val lodSplatLimit: Int? = null,
)

/** A turn in place through `startBenchmark`. */
data class RunSchedule(val seconds: Float)

/** What a launch intent asks for, decided from its extras alone. */
sealed interface Launch {
    /** No extras: the bundled world, interactive. */
    data object Default : Launch

    /** [json] is the run extra re-encoded compactly, for the provenance log. */
    data class Run(val file: RunFile, val json: String) : Launch

    data class Invalid(val message: String) : Launch

    companion object {
        /**
         * [run] is the run extra as a string, null when absent or of another type. Namespaced
         * extras such as `android.intent.extra.REFERRER` come from the system or a launcher.
         */
        fun fromExtras(keys: Set<String>, run: String?): Launch {
            val unknown = keys.filter { it != RUN_EXTRA && '.' !in it }.sorted()
            if (unknown.isNotEmpty()) {
                return Invalid("unknown extra ${unknown.joinToString()}; pass settings in the $RUN_EXTRA extra")
            }
            if (RUN_EXTRA !in keys) return Default
            if (run == null) return Invalid("the $RUN_EXTRA extra must be a string (--es)")
            return try {
                val document = RunFile.parse(run)
                Run(RunFile.decode(document), document.toString())
            } catch (e: RunFileException) {
                Invalid(e.message.orEmpty())
            }
        }
    }
}

private fun world(element: JsonElement): RunWorld {
    val fields = element.fields(
        Key.WORLD,
        known = setOf(Key.FILE, Key.SOURCE_FRAME, Key.COLLIDER),
        required = setOf(Key.FILE),
    )
    val sourceFramePath = path(Key.WORLD, Key.SOURCE_FRAME)
    return RunWorld(
        file = fields.getValue(Key.FILE).nonEmptyString(path(Key.WORLD, Key.FILE)),
        sourceFrame = fields[Key.SOURCE_FRAME]?.let { value ->
            val letters = value.string(sourceFramePath)
            CoordinateFrame.parse(letters) ?: fail(sourceFramePath, "must name each of rl, ud and bf once")
        },
        collider = fields[Key.COLLIDER]?.nonEmptyString(path(Key.WORLD, Key.COLLIDER)),
    )
}

private fun renderer(element: JsonElement): RunRenderer {
    val fields = element.fields(
        Key.RENDERER,
        known = setOf(Key.QUALITY, Key.BUDGET, Key.SUBPIXEL, Key.LOD_ERROR_PIXELS, Key.LOD_SPLAT_LIMIT),
    )
    fun at(key: String) = path(Key.RENDERER, key)
    return RunRenderer(
        quality = fields[Key.QUALITY]?.let { value ->
            // RenderQuality.named ignores case; the schema does not.
            value.string(at(Key.QUALITY)).takeIf { it == it.lowercase() }?.let(RenderQuality::named)
                ?: fail(at(Key.QUALITY), "is not an Android quality level")
        },
        budget = fields[Key.BUDGET]?.let { it.integer(at(Key.BUDGET)).atLeast(0, at(Key.BUDGET)) },
        subpixel = fields[Key.SUBPIXEL]?.let { it.number(at(Key.SUBPIXEL)).atLeast(0f, at(Key.SUBPIXEL)) },
        lodErrorPixels = fields[Key.LOD_ERROR_PIXELS]?.let { it.number(at(Key.LOD_ERROR_PIXELS)).positive(at(Key.LOD_ERROR_PIXELS)) },
        lodSplatLimit = fields[Key.LOD_SPLAT_LIMIT]?.let { it.integer(at(Key.LOD_SPLAT_LIMIT)).atLeast(0, at(Key.LOD_SPLAT_LIMIT)) },
    )
}

private fun pose(element: JsonElement): CameraPose {
    val (x, y, z, yaw, pitch) = element.numbers(Key.POSE, POSE_LENGTH)
    return CameraPose(x, y, z, yaw, pitch)
}

private fun schedule(element: JsonElement): RunSchedule {
    val fields = element.fields(
        Key.RUN,
        known = setOf(Key.SECONDS),
        required = setOf(Key.SECONDS),
        iosOnly = setOf(Key.ORBIT, Key.CAPTURE_AT, Key.MONITOR),
    )
    val secondsPath = path(Key.RUN, Key.SECONDS)
    return RunSchedule(fields.getValue(Key.SECONDS).number(secondsPath).positive(secondsPath))
}

private fun fail(path: String, reason: String): Nothing = throw RunFileException(path, reason)

private fun path(parent: String, key: String) = if (parent.isEmpty()) key else "$parent.$key"

/** The object's fields after the required ones are checked present and every key known. */
private fun JsonElement.fields(
    at: String,
    known: Set<String>,
    required: Set<String> = emptySet(),
    iosOnly: Set<String> = emptySet(),
): JsonObject {
    val fields = this as? JsonObject ?: fail(at, "must be an object")
    required.firstOrNull { it !in fields }?.let { fail(path(at, it), "missing") }
    fields.keys.forEach { key ->
        when (key) {
            in known -> Unit
            in iosOnly -> fail(path(at, key), "not supported on Android")
            else -> fail(path(at, key), "unknown key")
        }
    }
    return fields
}

/** The literal text of a JSON number, rejecting strings, booleans and null. */
private fun JsonElement.numberText(at: String): String {
    val text = (this as? JsonPrimitive)?.takeIf { it !is JsonNull && !it.isString }?.content
    if (text == null || !JSON_NUMBER.matches(text)) fail(at, "must be a number")
    return text
}

private fun JsonElement.number(at: String): Float {
    val value = numberText(at).toFloat()
    if (!value.isFinite()) fail(at, "out of range")
    return value
}

private fun JsonElement.integer(at: String): Int {
    val value = BigDecimal(numberText(at))
    if (value.stripTrailingZeros().scale() > 0) fail(at, "must be an integer")
    return try {
        value.intValueExact()
    } catch (e: ArithmeticException) {
        fail(at, "out of range")
    }
}

private fun JsonElement.string(at: String): String =
    (this as? JsonPrimitive)?.takeIf { it.isString }?.content ?: fail(at, "must be a string")

private fun JsonElement.nonEmptyString(at: String): String = string(at).ifEmpty { fail(at, "must not be empty") }

private fun JsonElement.numbers(at: String, count: Int): List<Float> {
    val items = this as? JsonArray ?: fail(at, "must be an array of $count numbers")
    if (items.size != count) fail(at, "must hold $count numbers")
    return items.mapIndexed { index, item -> item.number(path(at, index.toString())) }
}

private fun <T : Comparable<T>> T.atLeast(minimum: T, at: String) = also { if (it < minimum) fail(at, "must be at least $minimum") }


private fun Float.positive(at: String) = also { if (it <= 0f) fail(at, "must be greater than 0") }
