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
  assert.match(view, /emitCapabilities\(view\)\s*\/\/[^\n]*\n\s*applyPolicy\(view\)\s*view\.loadWorld/);
  assert.match(view, /token == session\.generation\) emitPolicy\(requested\.revision/);
  const prop = read('android/src/main/java/com/splatkit/reactnative/PolicyProp.kt');
  assert.match(prop, /INVALID_POLICY/);
  assert.match(prop, /POLICY_PREPARATION_FAILED/);
});
