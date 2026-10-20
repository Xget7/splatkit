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
  assert.match(view, /emitCapabilities\(view\)[\s\S]{0,400}?applyPolicy\(view\)[\s\S]{0,400}?scheduleDecode\(view, world\)/);
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
  assert.match(view, /scheduleDecode\(view, world\)\s*\/\/[^\n]*\n\s*loadCollider\(\)/);
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
  assert.match(view, /applyCamera\(view, replayAccepted = true\)[\s\S]*scheduleDecode\(view, world\)/);
});

// The SDK half of the transaction lives in the Android SDK, which the published package's
// repository does not carry.
test('Android SDK applies a pending camera once the world is ready', t => {
  const sdk = '../splatkit-android/src/main/kotlin/com/splatkit/engine/RenderThread.kt';
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

// A lens the JS contract accepts but the adapter refuses fails the load with no field named.
test('the Android adapter accepts the lens ranges the JS contract declares', () => {
  const kotlin = read('android/src/main/java/com/splatkit/reactnative/WorldSession.kt');
  const constant = name => {
    const match = kotlin.match(new RegExp(`const val ${name} = (\\d[\\d_]*)f`));
    assert.ok(match, `WorldSession must state ${name}`);
    return Number(match[1].replace(/_/g, ''));
  };
  const validation = require('../build/validation.js');
  for (const name of ['MIN_FIELD_OF_VIEW_DEGREES', 'MAX_FIELD_OF_VIEW_DEGREES',
    'MAX_DEPTH_RANGE_RATIO']) {
    assert.equal(constant(name), validation[name], name);
  }
  // The engine the world builds takes the lens before its decode is queued.
  const view = read('android/src/main/java/com/splatkit/reactnative/SplatKitView.kt');
  assert.match(view, /if \(!applyLens\(view, world, token\)\) return\s*scheduleDecode\(view, world\)/);
  assert.match(view, /fun applyLens[^{]*\{\s*if \(view\.setLens\(world\.lens\)\) return true/);
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

test('Android adapter streams a tileset and loads any other world as one file', () => {
  const kotlin = read('android/src/main/java/com/splatkit/reactnative/Wire.kt');
  const manager = read('android/src/main/java/com/splatkit/reactnative/SplatKitViewManager.kt');
  const view = read('android/src/main/java/com/splatkit/reactnative/SplatKitView.kt');
  const session = read('android/src/main/java/com/splatkit/reactnative/WorldSession.kt');
  const {WorldKind} = require('../build/contracts.js');
  const body = kotlin.match(/object WorldKind \{([^}]*)\}/);
  assert.ok(body, 'Wire.kt must declare WorldKind');
  const numbered = name => Number(body[1].match(new RegExp(`const val ${name} = (\\d+)`))?.[1]);
  assert.equal(numbered('FILE'), WorldKind.file);
  assert.equal(numbered('TILESET'), WorldKind.tileset);
  // Codegen leaves an absent optional field out of the map; that request is a file.
  assert.match(manager, /hasKey\("kind"\)[^\n]*else WorldKind\.FILE/);
  assert.match(session, /require\(kind == WorldKind\.FILE \|\| kind == WorldKind\.TILESET\)/);
  // The SDK's tiled load takes no options, so the budgets it reads at upload are set first.
  assert.match(view, /WorldKind\.TILESET\) \{\s*(\/\/[^\n]*\s*)*view\.maxShDegree = world\.maxShDegree\s*view\.residencyBudget = world\.residencyCapacitySplats\s*view\.loadTiledWorld\(file\)/);
  assert.match(view, /\} else \{\s*view\.loadWorld\(file, world\.maxShDegree, world\.lodCapacitySplats, world\.residencyCapacitySplats\)/);
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
