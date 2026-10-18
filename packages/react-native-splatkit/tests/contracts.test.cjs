const {test} = require('node:test');
const assert = require('node:assert/strict');
// Keep contract tests runnable in Node; the package entry also exports the
// Fabric host component, which intentionally requires React Native at runtime.
const {validateWorldRequest, validateRenderOptions, optionalTimingMillis} = require('../build/contracts');

const limits = {maxLodCapacitySplats: 2200000, minResidencyCapacitySplats: 100000,
  maxResidencyCapacitySplats: 8000000};
const request = Object.freeze({requestId: 'load-1', filePath: '/documents/world.spz',
  maxShDegree: 3, lodCapacitySplats: 1200000, residencyCapacitySplats: 2000000});

test('accepts a local immutable request without mutating or silently clamping it', () => {
  assert.doesNotThrow(() => validateWorldRequest(request, limits));
  assert.equal(request.lodCapacitySplats, 1200000);
});

test('a world is one file or a tileset, and a request without a kind is a file', () => {
  const {WorldKind} = require('../build/contracts');
  assert.deepEqual(WorldKind, {file: 0, tileset: 1});
  for (const kind of [undefined, WorldKind.file, WorldKind.tileset]) {
    assert.doesNotThrow(() => validateWorldRequest({...request, kind}, limits));
  }
  for (const kind of [2, -1, 'tileset', 0.5, NaN, null]) {
    assert.throws(() => validateWorldRequest({...request, kind}, limits), RangeError);
  }
});

test('rejects remote, relative, root, network and NUL-containing paths', () => {
  for (const filePath of ['https://example.com/world.spz', 'file:///world.spz',
    'world.spz', '/', '//server/world.spz', '/world\0.spz', '']) {
    assert.throws(() => validateWorldRequest({...request, filePath}, limits), TypeError);
  }
});

test('requires request identity for stale-result rejection in future adapters', () => {
  for (const requestId of ['', ' ', null]) {
    assert.throws(() => validateWorldRequest({...request, requestId}, limits), TypeError);
  }
});

test('validates SH degree rather than allowing NaN, fractional or unsupported values', () => {
  for (const maxShDegree of [-1, 4, 1.5, NaN, Infinity]) {
    assert.throws(() => validateWorldRequest({...request, maxShDegree}, limits), RangeError);
  }
});

test('selection capacity uses native limits and permits explicit zero', () => {
  assert.doesNotThrow(() => validateWorldRequest({...request, lodCapacitySplats: 0}, limits));
  for (const lodCapacitySplats of [-1, 2200001, 1.5, NaN, Infinity]) {
    assert.throws(() => validateWorldRequest({...request, lodCapacitySplats}, limits), RangeError);
  }
});

test('residency is a separate bounded count, not a byte or quality budget', () => {
  for (const residencyCapacitySplats of [0, 99999, 8000001, NaN]) {
    assert.throws(() => validateWorldRequest({...request, residencyCapacitySplats}, limits), RangeError);
  }
  assert.doesNotThrow(() => validateWorldRequest({...request, residencyCapacitySplats: 8000000}, limits));
});

test('a lens field left out keeps the native default, and a set one is in range', () => {
  assert.doesNotThrow(() => validateWorldRequest({...request, fieldOfViewDegrees: 10}, limits));
  assert.doesNotThrow(() => validateWorldRequest({...request, fieldOfViewDegrees: 120}, limits));
  for (const fieldOfViewDegrees of [0, 9.99, 120.01, NaN, Infinity, -65]) {
    assert.throws(() => validateWorldRequest({...request, fieldOfViewDegrees}, limits), RangeError);
  }
});

test('the depth range comes as a pair the shaders can read back', () => {
  assert.doesNotThrow(() =>
    validateWorldRequest({...request, nearPlane: 0.5, farPlane: 50000}, limits));
  for (const planes of [{nearPlane: 0.05}, {farPlane: 1000}]) {
    assert.throws(() => validateWorldRequest({...request, ...planes}, limits), TypeError);
  }
  for (const [nearPlane, farPlane] of [[0, 10], [-1, 10], [1, 1], [2, 1], [NaN, 10],
    [1, NaN], [1, Infinity], [0.5, 50001], [1e-50, 1], [1, 1 + 1e-12]]) {
    assert.throws(() => validateWorldRequest({...request, nearPlane, farPlane}, limits),
      RangeError, `${nearPlane}..${farPlane}`);
  }
});

// The iOS adapter defers to the engine's own check, so its ranges are the contract's. The
// engine sources are only available in the monorepo.
test('the engine accepts the lens ranges the contract declares', t => {
  const fs = require('node:fs');
  const path = require('node:path');
  const header = path.join(__dirname, '../../splatkit-engine/include/splatkit/camera/Lens.h');
  if (!fs.existsSync(header)) {
    t.skip('the engine sources are only available in the monorepo');
    return;
  }
  const lens = fs.readFileSync(header, 'utf8');
  const constant = name => Number(lens.match(new RegExp(`constexpr float ${name} = (\\d+);`))?.[1]);
  const validation = require('../build/validation.js');
  assert.equal(constant('kMinFieldOfViewDegrees'), validation.MIN_FIELD_OF_VIEW_DEGREES);
  assert.equal(constant('kMaxFieldOfViewDegrees'), validation.MAX_FIELD_OF_VIEW_DEGREES);
  assert.equal(constant('kMaxDepthRatio'), validation.MAX_DEPTH_RANGE_RATIO);
});

test('rejects malformed native limits before accepting requests', () => {
  for (const invalid of [{maxLodCapacitySplats: NaN}, {maxLodCapacitySplats: 2 ** 31},
    {minResidencyCapacitySplats: 0}, {maxResidencyCapacitySplats: 99999}]) {
    assert.throws(() => validateWorldRequest(request, {...limits, ...invalid}), RangeError);
  }
});

test('render options have explicit finite ranges', () => {
  assert.doesNotThrow(() => validateRenderOptions({paused: false, renderScale: 1, shDegree: 0}));
  for (const renderScale of [0, 0.09, 2.01, NaN, Infinity]) {
    assert.throws(() => validateRenderOptions({paused: false, renderScale, shDegree: 0}), RangeError);
  }
  assert.throws(() => validateRenderOptions({paused: 'false', renderScale: 1, shDegree: 0}), TypeError);
});

test('unavailable is null, while a measured zero remains zero', () => {
  assert.equal(optionalTimingMillis(false, 0), null);
  assert.equal(optionalTimingMillis(false, 15), null);
  assert.equal(optionalTimingMillis(true, 0), 0);
  assert.equal(optionalTimingMillis(true, 15), 15);
  for (const value of [NaN, Infinity, -1]) assert.equal(optionalTimingMillis(true, value), null);
});

test('collider requests and character settings are validated before they reach native', () => {
  const {validateColliderRequest, validateCharacter} = require('../build/contracts.js');
  validateColliderRequest({requestId: 'c1', filePath: '/tmp/room.glb'});
  assert.throws(() => validateColliderRequest({requestId: ' ', filePath: '/tmp/room.glb'}), TypeError);
  assert.throws(() => validateColliderRequest({requestId: 'c1', filePath: 'https://x/room.glb'}), TypeError);
  validateCharacter({eyeHeight: 1.5, bodyRadius: 0.35, stepHeight: 0.35});
  assert.throws(() => validateCharacter({eyeHeight: 0, bodyRadius: 0.35, stepHeight: 0.35}), RangeError);
  // A body wider than the walker is tall, or a step it could never reach, is not a walker.
  assert.throws(() => validateCharacter({eyeHeight: 1.5, bodyRadius: 2, stepHeight: 0.35}), RangeError);
  assert.throws(() => validateCharacter({eyeHeight: 1.5, bodyRadius: 0.35, stepHeight: 1.5}), RangeError);
});

const {CameraMode, toNativeCameraProp, validateCameraRequest} = require('../build/contracts');
const orbit = Object.freeze({revision: 1, mode: CameraMode.orbit,
  anchor: Object.freeze({x: 1, y: 2, z: 3}), radius: 4, azimuth: 0.2,
  elevation: 0.5, orbitRadiansPerSecond: -0.1});

test('camera requests serialize without mutation and first person has canonical wire fields', () => {
  assert.deepEqual(toNativeCameraProp(orbit), {revision: 1, mode: 1,
    anchorX: 1, anchorY: 2, anchorZ: 3, radius: 4, azimuth: 0.2,
    elevation: 0.5, orbitRadiansPerSecond: -0.1});
  assert.deepEqual(toNativeCameraProp({revision: 2, mode: CameraMode.firstPerson}), {
    revision: 2, mode: 0, anchorX: 0, anchorY: 0, anchorZ: 0, radius: 1,
    azimuth: 0, elevation: 0, orbitRadiansPerSecond: 0});
  assert.equal(orbit.anchor.x, 1);
});

test('camera rejects invalid revisions, modes and native float overflow', () => {
  for (const revision of [0, -1, 1.5, 2 ** 31, NaN, Infinity]) {
    assert.throws(() => validateCameraRequest({...orbit, revision}), RangeError);
  }
  for (const mode of [-1, 2, 'orbit', NaN]) {
    assert.throws(() => validateCameraRequest({...orbit, mode}), RangeError);
  }
  for (const field of ['radius', 'azimuth', 'elevation', 'orbitRadiansPerSecond']) {
    for (const value of [NaN, Infinity, -Infinity, 1e100, undefined]) {
      assert.throws(() => validateCameraRequest({...orbit, [field]: value}), RangeError);
    }
  }
  for (const radius of [0, -1, 1e-100]) {
    assert.throws(() => validateCameraRequest({...orbit, radius}), RangeError);
  }
  for (const anchor of [null, undefined, {x: Infinity, y: 0, z: 0}, {x: 0, y: 0}]) {
    assert.throws(() => validateCameraRequest({...orbit, anchor}));
  }
  validateCameraRequest({...orbit, elevation: 10, orbitRadiansPerSecond: 0});
});

test('camera mode values match the engine and native SDK contracts', t => {
  const fs = require('node:fs');
  const path = require('node:path');
  const read = file => fs.readFileSync(path.join(__dirname, '../../', file), 'utf8');
  if (!fs.existsSync(path.join(__dirname, '../../splatkit-engine'))) {
    t.skip('native source contracts only available in the monorepo');
    return;
  }
  const cpp = read('splatkit-engine/include/splatkit/camera/CameraRequest.h');
  const kotlin = read('splatkit-android/src/main/java/com/splatkit/CameraRequest.kt');
  const objc = read('splatkit-ios/Sources/SplatKitCore/include/SplatKit/SKSplatEngine.h');
  for (const [name, cppName, kotlinName, objcName] of [
    ['firstPerson', 'FirstPerson', 'FIRST_PERSON', 'SKCameraModeFirstPerson'],
    ['orbit', 'Orbit', 'ORBIT', 'SKCameraModeOrbit'],
  ]) {
    assert.ok(cpp.includes(`${cppName} = ${CameraMode[name]}`));
    assert.ok(kotlin.includes(`${kotlinName}(${CameraMode[name]})`));
    assert.ok(objc.includes(`${objcName} = ${CameraMode[name]}`));
  }
});
