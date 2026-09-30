const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, '..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8');

test('Android adapter maps the versioned policy prop and its events', () => {
  const manager = read('android/src/main/java/com/splatkit/reactnative/SplatKitViewManager.kt');
  const view = read('android/src/main/java/com/splatkit/reactnative/SplatKitView.kt');
  assert.match(manager, /override fun setPolicy/);
  assert.match(manager, /parsePolicyProp\(value\)/);
  assert.match(manager, /EventType\.POLICY to mapOf\("registrationName" to "onPolicyEvent"\)/);
  assert.match(manager, /EventType\.CAPABILITIES to mapOf\("registrationName" to "onCapabilities"\)/);
  // Props commit per transaction, so a world and a policy changed together apply once.
  assert.match(manager, /override fun onAfterUpdateTransaction[\s\S]*commitProps\(\)/);
  // The stored policy is applied to each engine a world builds, after capabilities and
  // before the load, and outcomes from a replaced engine are dropped.
  assert.match(view, /emitCapabilities\(view\)[\s\S]{0,400}?applyPolicy\(view\)[\s\S]{0,400}?view\.loadWorld/);
  assert.match(view, /token == session\.generation\) emitPolicy\(requested\.revision/);
  const prop = read('android/src/main/java/com/splatkit/reactnative/PolicyProp.kt');
  assert.match(prop, /INVALID_POLICY/);
  assert.match(prop, /POLICY_PREPARATION_FAILED/);
});

test('Android adapter exposes navigation as props, commands and collider events', () => {
  const manager = read('android/src/main/java/com/splatkit/reactnative/SplatKitViewManager.kt');
  const view = read('android/src/main/java/com/splatkit/reactnative/SplatKitView.kt');
  for (const method of ['setCollider', 'setCharacter', 'setMotionEnabled', 'setTouchLookEnabled',
    'setLookSensitivity', 'setCameraPoseInterval', 'setWalkVelocity', 'look', 'setCameraPose']) {
    assert.match(manager, new RegExp(`override fun ${method}\\(`), method);
  }
  assert.match(manager, /EventType\.COLLIDER to mapOf\("registrationName" to "onColliderEvent"\)/);
  assert.match(manager, /EventType\.CAMERA_POSE to mapOf\("registrationName" to "onCameraPose"\)/);
  // A world builds a new engine, so walk mode is rebuilt on it after the load is queued.
  assert.match(view, /view\.loadWorld\([\s\S]*loadCollider\(\)/);
  assert.match(view, /COLLIDER_LOAD_FAILED/);
  // Commands reach the SDK view directly, with no React commit per frame.
  assert.match(view, /fun walk\(forward: Double, right: Double\) \{\s*nativeView\?\.setWalkVelocity/);
});

test('Android camera transaction survives world replacement', () => {
  const manager = read('android/src/main/java/com/splatkit/reactnative/SplatKitViewManager.kt');
  const view = read('android/src/main/java/com/splatkit/reactnative/SplatKitView.kt');
  assert.match(manager, /override fun setCamera\(/);
  assert.match(manager, /EventType\.CAMERA to mapOf\("registrationName" to "onCameraEvent"\)/);
  assert.match(view, /value\.revision == cameraRevisionSeen/);
  assert.match(view, /listOfNotNull\(cameraAccepted, cameraPending\)/);
  assert.match(view, /token != session\.generation/);
  assert.match(view, /if \(resolution\.accepted\) \{\s*cameraAccepted = requested/);
  assert.match(view, /applyCamera\(view, replayAccepted = true\)[\s\S]*view\.loadWorld/);
});

// The SDK half of the transaction lives in the Android SDK, which the published package's
// repository does not carry.
test('Android SDK applies a pending camera once the world is ready', t => {
  const sdk = '../splatkit-android/src/main/java/com/splatkit/engine/RenderThread.kt';
  if (!fs.existsSync(path.join(root, sdk))) {
    t.skip('the Android SDK sources are only available in the monorepo');
    return;
  }
  const render = read(sdk);
  assert.match(render, /event == SplatEngine\.Event\.WORLD_READY[\s\S]*applyPendingCamera\(\)/);
  assert.match(render, /Looper\.myLooper\(\) == thread\.looper\) markWorldReady\(\)/);
  assert.match(render, /if \(!worldReady\) return/);
});

// A request built against limits the adapter refuses is rejected before any engine exists, so
// no capabilities event follows and the host cannot learn what it got wrong: the first guess
// has to be one every adapter accepts.
test('conservativeCapabilities fit the ranges the Android adapter accepts', () => {
  const kotlin = read('android/src/main/java/com/splatkit/reactnative/WorldSession.kt');
  const constant = name => {
    const match = kotlin.match(new RegExp(`const val ${name} = (\\d[\\d_]*)`));
    assert.ok(match, `WorldSession must state ${name}`);
    return Number(match[1].replace(/_/g, ''));
  };

  const {limits} = require('../build/performance.js').conservativeCapabilities;
  assert.ok(limits.maxLodCapacitySplats <= constant('MAX_LOD_CAPACITY_SPLATS'),
    `maxLodCapacitySplats ${limits.maxLodCapacitySplats} exceeds the adapter's`);
  assert.ok(limits.minResidencyCapacitySplats >= constant('MIN_RESIDENCY_CAPACITY_SPLATS'),
    'minResidencyCapacitySplats falls below the adapter minimum');
  assert.ok(limits.maxResidencyCapacitySplats <= constant('MAX_RESIDENCY_CAPACITY_SPLATS'),
    'maxResidencyCapacitySplats exceeds the adapter maximum');
});

// The wire spellings exist in the JS contracts, the codegen spec and each adapter; Kotlin
// compares against its own constants, so a drifted spelling has to fail here.
test('Android wire constants spell the values the JS contracts declare', () => {
  const kotlin = read('android/src/main/java/com/splatkit/reactnative/Wire.kt');
  const {WorldPhase, ColliderPhase, CameraPhase} = require('../build/contracts.js');
  const {PolicyPhase} = require('../build/performance.js');
  const spelled = name => {
    const body = kotlin.match(new RegExp(`object ${name} \\{([^}]*)\\}`));
    assert.ok(body, `Wire.kt must declare ${name}`);
    return [...body[1].matchAll(/const val \w+ = "([^"]*)"/g)].map(match => match[1]).sort();
  };
  assert.deepEqual(spelled('WorldPhase'), Object.values(WorldPhase).sort());
  assert.deepEqual(spelled('ColliderPhase'), Object.values(ColliderPhase).sort());
  assert.deepEqual(spelled('CameraPhase'), Object.values(CameraPhase).sort());
  assert.deepEqual(spelled('PolicyPhase'), Object.values(PolicyPhase).sort());
});

// The adapter reported every timing as unavailable, so a HUD on Android could never show a
// frame rate or a GPU time even while the engine was measuring both.
test('Android adapter reports the timings the SDK measures', () => {
  const kotlin = fs.readFileSync(
    path.join(root, 'android/src/main/java/com/splatkit/reactnative/SplatKitView.kt'), 'utf8');
  for (const field of ['frame', 'gpu', 'sort']) {
    assert.match(kotlin, new RegExp(`putDouble\\("${field}Millis", stats\\.${field}Millis`));
    assert.match(kotlin,
      new RegExp(`putBoolean\\("${field}TimingAvailable", stats\\.${field}Millis > 0f\\)`));
  }
});
