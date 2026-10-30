#!/usr/bin/env python3
"""Build and check the run file the iOS and Android dev apps take as --run or the run extra.

benchmarks/run.schema.json defines it; benchmarks/run-samples.json holds the cases every
platform's decoder is tested against.
"""

import json
import math
from pathlib import Path
import re

BENCHMARKS = Path(__file__).resolve().parent / "benchmarks"
SCHEMA = BENCHMARKS / "run.schema.json"
SAMPLES = BENCHMARKS / "run-samples.json"
SCHEMA_VERSION = 1
POSE_LENGTH = 5
# What the perf scripts save beside a run's results.
SAVED_NAME = "run.json"
# The JSON Schema keywords validate() enforces; anything else in the schema would go unchecked.
CHECKED = {"type", "const", "enum", "properties", "additionalProperties", "required", "minLength", "pattern",
           "minimum", "maximum", "exclusiveMinimum", "items", "minItems", "maxItems", "not"}
ANNOTATIONS = {"$schema", "title", "description"}


class RunFileError(ValueError):
    """A run file the apps would refuse; key is the dotted path they report."""

    def __init__(self, key, problem):
        super().__init__(f"{key}: {problem}")
        self.key = key


def load_schema():
    return json.loads(SCHEMA.read_text())


def check_keywords(schema):
    unknown = set(schema) - CHECKED - ANNOTATIONS
    if unknown:
        raise ValueError(f"run.schema.json uses unchecked keywords: {sorted(unknown)}")
    for child in schema.get("properties", {}).values():
        check_keywords(child)
    for key in ("items", "not"):
        if key in schema:
            check_keywords(schema[key])


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


TYPES = {
    "object": lambda v: isinstance(v, dict),
    "array": lambda v: isinstance(v, list),
    "string": lambda v: isinstance(v, str),
    "boolean": lambda v: isinstance(v, bool),
    "number": is_number,
    "integer": lambda v: is_number(v) and float(v).is_integer(),
}


def matches(value, schema, key):
    try:
        check(value, schema, key)
        return True
    except RunFileError:
        return False


def check(value, schema, key):
    """Raises RunFileError at the first key value breaks schema at."""
    if "type" in schema and not TYPES[schema["type"]](value):
        raise RunFileError(key, f"expected {schema['type']}")
    if "const" in schema and value != schema["const"]:
        raise RunFileError(key, f"expected {schema['const']}")
    if "enum" in schema and value not in schema["enum"]:
        raise RunFileError(key, f"expected one of {', '.join(schema['enum'])}")
    if "not" in schema and matches(value, schema["not"], key):
        raise RunFileError(key, "value not allowed")
    if "minLength" in schema and len(value) < schema["minLength"]:
        raise RunFileError(key, "empty")
    # ECMA-262 $ matches only at the end; Python's also matches before a final newline.
    if "pattern" in schema and not re.search(re.sub(r"\$$", r"\\Z", schema["pattern"]), value):
        raise RunFileError(key, f"expected {schema['pattern']}")
    if "minimum" in schema and value < schema["minimum"]:
        raise RunFileError(key, f"below {schema['minimum']}")
    if "maximum" in schema and value > schema["maximum"]:
        raise RunFileError(key, f"above {schema['maximum']}")
    if "exclusiveMinimum" in schema and value <= schema["exclusiveMinimum"]:
        raise RunFileError(key, f"must exceed {schema['exclusiveMinimum']}")
    if isinstance(value, list):
        if not schema.get("minItems", 0) <= len(value) <= schema.get("maxItems", len(value)):
            raise RunFileError(key, "wrong number of items")
        for item in value:
            check(item, schema.get("items", {}), key)
    if isinstance(value, dict):
        prefix = f"{key}." if key else ""
        properties = schema.get("properties", {})
        if schema.get("additionalProperties") is False:
            for name in value:
                if name not in properties:
                    raise RunFileError(prefix + name, "unknown key")
        for name in schema.get("required", []):
            if name not in value:
                raise RunFileError(prefix + name, "missing")
        for name, child in properties.items():
            if name in value:
                check(value[name], child, prefix + name)


def validate(doc, schema=None):
    check(doc, schema or load_schema(), "")


def document(seconds, quality, pose=None, world=None, source_frame=None, budget=None, lod_splat_limit=None,
             subpixel=None):
    """A benchmark run file from the perf scripts' options, checked against the schema.

    pose is the scripts' "x,y,z,yaw,pitch" string.
    """
    if source_frame and not world:
        raise ValueError("a source frame names the frame of a world file; pass the world too")
    doc = {"schema_version": SCHEMA_VERSION}
    if world:
        doc["world"] = {"file": world, **({"source_frame": source_frame} if source_frame else {})}
    renderer = {"quality": quality}
    for name, value in (("budget", budget), ("lod_splat_limit", lod_splat_limit), ("subpixel", subpixel)):
        if value is not None:
            renderer[name] = value
    doc["renderer"] = renderer
    if pose:
        values = [float(part) for part in pose.split(",")]
        if len(values) != POSE_LENGTH:
            raise ValueError(f"a pose is x,y,z,yaw,pitch; got {pose}")
        doc["pose"] = [int(v) if v.is_integer() else v for v in values]
    doc["run"] = {"seconds": seconds}
    validate(doc)
    return doc


def encode(doc):
    """One line, as the apps take it in a single argument."""
    return json.dumps(doc, separators=(",", ":"), allow_nan=False)


def save(doc, directory):
    (Path(directory) / SAVED_NAME).write_text(encode(doc) + "\n")
