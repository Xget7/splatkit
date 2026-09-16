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
