const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, '..');
test('iOS adapter is Fabric-only and owns the native view lifecycle', () => {
  const mm = fs.readFileSync(path.join(root, 'ios/SplatKitViewComponentView.mm'), 'utf8');
  const native = fs.readFileSync(path.join(root, 'ios/SplatKitRNView.mm'), 'utf8');
  assert.match(fs.readFileSync(path.join(root, 'ios/SplatKitViewComponentView.h'), 'utf8'), /RCTViewComponentView/);
  assert.match(mm, /componentDescriptorProvider/);
  assert.doesNotMatch(mm, /RCTBridgeModule|RCT_EXPORT_MODULE|RCTViewManager/);
  assert.match(native, /SKSplatEngine/);
  assert.match(native, /CADisplayLink/);
});

test('iOS adapter decodes off the render thread and gates events by generation', () => {
  const native = fs.readFileSync(path.join(root, 'ios/SplatKitRNView.mm'), 'utf8');
  assert.match(native, /dispatch_queue_create/);
  assert.match(native, /generation/);
  assert.match(native, /memory_order_acquire/);
});

test('iOS adapter shares Android failure codes and throttles stats', () => {
  const native = fs.readFileSync(path.join(root, 'ios/SplatKitRNView.mm'), 'utf8');
  assert.match(native, /INVALID_REQUEST/);
  assert.match(native, /WORLD_LOAD_FAILED/);
  assert.match(native, /GPU_UNAVAILABLE/);
  assert.match(native, /kStatsInterval/);
});

test('iOS component view releases the engine on recycle and invalidate', () => {
  const mm = fs.readFileSync(path.join(root, 'ios/SplatKitViewComponentView.mm'), 'utf8');
  const native = fs.readFileSync(path.join(root, 'ios/SplatKitRNView.mm'), 'utf8');
  assert.match(mm, /prepareForRecycle[\s\S]*\[_splatView recycle\]/);
  assert.match(mm, /invalidate[\s\S]*\[_splatView dispose\]/);
  // Recycling forgets the policy so the next mount starts from its own props.
  assert.match(native, /- \(void\)recycle \{[\s\S]*_hasPolicy = NO/);
});

test('iOS adapter applies the versioned policy prop and reports it by revision', () => {
  const mm = fs.readFileSync(path.join(root, 'ios/SplatKitViewComponentView.mm'), 'utf8');
  const native = fs.readFileSync(path.join(root, 'ios/SplatKitRNView.mm'), 'utf8');
  assert.match(mm, /next\.policy\.revision/);
  assert.match(mm, /setPolicy:policy revision:/);
  assert.match(mm, /onPolicyEvent/);
  assert.match(mm, /onCapabilities/);
  // A world load applies the stored policy itself, so a combined update applies it once.
  assert.match(mm, /loadWorld:[\s\S]*return;[\s\S]*if \(policyChanged\) \[_splatView applyStoredPolicy\]/);
  // Capabilities first, then the policy, then the decode: the Android order.
  assert.match(native, /\[self emitCapabilities\];\s*\[self applyStoredPolicy\];[\s\S]*loadWorldFile/);
  assert.match(native, /INVALID_POLICY/);
  assert.match(native, /POLICY_PREPARATION_FAILED/);
});

test('iOS adapter exposes navigation as props, commands and collider events', () => {
  const mm = fs.readFileSync(path.join(root, 'ios/SplatKitViewComponentView.mm'), 'utf8');
  const native = fs.readFileSync(path.join(root, 'ios/SplatKitRNView.mm'), 'utf8');
  // Looking is the only touch the view keeps; walking is the host's own control.
  assert.match(native, /UIPanGestureRecognizer/);
  assert.doesNotMatch(native, /minimumNumberOfTouches/);
  assert.match(native, /setWalkVelocityForward:/);
  assert.match(native, /COLLIDER_LOAD_FAILED/);
  // The collider is re-queued on the engine each world load creates.
  assert.match(native, /loadWorldFile[\s\S]*enqueueColliderOnGeneration/);
  assert.match(native, /startDeviceMotionUpdatesUsingReferenceFrame/);
  assert.match(mm, /handleCommand[\s\S]*setWalkVelocity[\s\S]*look[\s\S]*setCameraPose/);
  assert.match(mm, /onColliderEvent/);
  assert.match(mm, /onCameraPose/);
  // Walk mode is rebuilt before the world load that replaces the engine.
  assert.match(mm, /if \(colliderChanged\)[\s\S]*if \(worldChanged\)/);
});

// The props carry no lodSplatLimit, so an indeterminate one reached the selector and starved
// it to that many splats: the world rendered or vanished depending on the stack.
test('iOS adapter value initializes the C structs it fills from props', () => {
  const mm = fs.readFileSync(path.join(root, 'ios/SplatKitViewComponentView.mm'), 'utf8');
  for (const type of ['SKRenderPolicy', 'SKCharacterSettings']) {
    assert.match(mm, new RegExp(`${type} \\w+\\{\\}`), `${type} must be value initialized`);
    assert.doesNotMatch(mm, new RegExp(`${type} \\w+;`), `${type} must not be left indeterminate`);
  }
});

// A debug build asserts that a component view carries its own props type from construction,
// so a missing default aborted the app on the first commit while release builds ran.
test('iOS component view seeds _props with its own default props', () => {
  const mm = fs.readFileSync(path.join(root, 'ios/SplatKitViewComponentView.mm'), 'utf8');
  assert.match(mm, /initWithFrame:[\s\S]*std::make_shared<const SplatKitViewProps>\(\)/);
  assert.match(mm, /initWithFrame:[\s\S]*_props = defaultProps;/);
});
