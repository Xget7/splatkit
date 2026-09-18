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
  assert.match(manager, /"topPolicyEvent" to mapOf\("registrationName" to "onPolicyEvent"\)/);
  assert.match(manager, /"topCapabilities" to mapOf\("registrationName" to "onCapabilities"\)/);
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
  assert.match(manager, /"topColliderEvent" to mapOf\("registrationName" to "onColliderEvent"\)/);
  assert.match(manager, /"topCameraPose" to mapOf\("registrationName" to "onCameraPose"\)/);
  // A world builds a new engine, so walk mode is rebuilt on it after the load is queued.
  assert.match(view, /view\.loadWorld\([\s\S]*loadCollider\(\)/);
  assert.match(view, /COLLIDER_LOAD_FAILED/);
  // Commands reach the SDK view directly, with no React commit per frame.
  assert.match(view, /fun walk\(forward: Double, right: Double\) \{\s*nativeView\?\.setWalkVelocity/);
});

// A request built against limits the adapter refuses is rejected before any engine exists, so
// no capabilities event follows and the host cannot learn what it got wrong: the first guess
// has to be one every adapter accepts.
test('conservativeCapabilities fit the ranges the Android adapter accepts', () => {
  const kotlin = fs.readFileSync(
    path.join(root, 'android/src/main/java/com/splatkit/reactnative/WorldSession.kt'), 'utf8');
  const number = text => Number(text.replace(/_/g, ''));
  const lod = kotlin.match(/lodCapacitySplats in (\d[\d_]*)\.\.(\d[\d_]*)/);
  const residency = kotlin.match(/residencyCapacitySplats in (\d[\d_]*)\.\.(\d[\d_]*)/);
  assert.ok(lod && residency, 'WorldSession must state both accepted ranges');

  const {limits} = require('../build/performance.js').conservativeCapabilities;
  assert.ok(limits.maxLodCapacitySplats <= number(lod[2]),
    `maxLodCapacitySplats ${limits.maxLodCapacitySplats} exceeds the adapter's ${lod[2]}`);
  assert.ok(limits.minResidencyCapacitySplats >= number(residency[1]),
    'minResidencyCapacitySplats falls below the adapter minimum');
  assert.ok(limits.maxResidencyCapacitySplats <= number(residency[2]),
    'maxResidencyCapacitySplats exceeds the adapter maximum');
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
