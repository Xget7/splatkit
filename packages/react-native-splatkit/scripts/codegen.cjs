// Exercise the installed, pinned RN generators, not a hand-written schema mock.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {execFileSync} = require('node:child_process');

const root = path.resolve(__dirname, '..');
const {codegenConfig} = require('../package.json');
// Each run gets fresh output so stale generated files cannot make a check pass.
// The output is kept outside `build/` so it never ships in the npm tarball.
const output = fs.mkdtempSync(path.join(os.tmpdir(), 'splatkit-codegen-'));
const schemaPath = path.join(output, 'schema.json');
const combine = require.resolve('@react-native/codegen/lib/cli/combine/combine-js-to-schema-cli.js');
const generate = require.resolve('react-native/scripts/generate-specs-cli');
execFileSync(process.execPath, [combine, schemaPath, path.join(root, codegenConfig.jsSrcsDir)],
  {stdio: 'inherit'});
const schema = JSON.parse(fs.readFileSync(schemaPath, 'utf8'));
const spec = schema.modules.SplatKitView?.components?.SplatKitView;
assert.ok(spec, 'Codegen must discover SplatKitView');
assert.deepEqual(spec.props.map(prop => prop.name).sort(),
  ['cameraPoseInterval', 'character', 'collider', 'cullMarginDegrees', 'linearBlending',
    'lookSensitivity', 'motionEnabled', 'paused', 'policy', 'renderScale', 'shDegree',
    'touchLookEnabled', 'world']);
assert.deepEqual(spec.events.map(event => event.name).sort(),
  ['onCameraPose', 'onCapabilities', 'onColliderEvent', 'onFocusResult', 'onPolicyEvent', 'onStats', 'onWorldEvent']);
assert.deepEqual(spec.commands.map(command => command.name).sort(),
  ['animateOrbit', 'dolly', 'focus', 'look', 'lookAt', 'orbit', 'setAnchor',
    'setCameraPose', 'setWalkVelocity']);
assert.deepEqual(spec.commands.find(command => command.name === 'setWalkVelocity')
  .typeAnnotation.params.map(param => [param.name, param.typeAnnotation.type]),
  [['forward', 'DoubleTypeAnnotation'], ['right', 'DoubleTypeAnnotation']]);
const collider = spec.props.find(prop => prop.name === 'collider').typeAnnotation.properties;
assert.deepEqual(collider.map(prop => prop.name).sort(), ['filePath', 'requestId']);
const character = spec.props.find(prop => prop.name === 'character').typeAnnotation.properties;
assert.deepEqual(character.map(prop => prop.name).sort(),
  ['bodyRadius', 'eyeHeight', 'stepHeight']);
const pose = spec.events.find(event => event.name === 'onCameraPose').typeAnnotation.argument.properties;
assert.deepEqual(pose.map(prop => prop.name).sort(), ['pitch', 'x', 'y', 'yaw', 'z']);
const focus = spec.events.find(event => event.name === 'onFocusResult').typeAnnotation.argument.properties;
assert.deepEqual(focus.map(prop => prop.name).sort(), ['hit', 'requestId']);
const world = spec.props.find(prop => prop.name === 'world').typeAnnotation.properties;
assert.deepEqual(world.map(prop => prop.name).sort(),
  ['filePath', 'lodCapacitySplats', 'maxShDegree', 'requestId', 'residencyCapacitySplats']);
assert.equal(world.find(prop => prop.name === 'lodCapacitySplats').typeAnnotation.type,
  'Int32TypeAnnotation');
const policy = spec.props.find(prop => prop.name === 'policy').typeAnnotation.properties;
assert.deepEqual(policy.map(prop => prop.name).sort(),
  ['alphaThreshold', 'enableEarlyTermination', 'enableFrustumCulling', 'enableHiZOcclusion',
    'lodErrorPixels', 'raster', 'revision', 'sortDepth', 'subpixelThreshold', 'tileSize']);
assert.equal(policy.find(prop => prop.name === 'revision').typeAnnotation.type,
  'Int32TypeAnnotation');
assert.equal(policy.find(prop => prop.name === 'lodErrorPixels').typeAnnotation.type,
  'DoubleTypeAnnotation');
const policyEvent =
  spec.events.find(event => event.name === 'onPolicyEvent').typeAnnotation.argument.properties;
assert.equal(policyEvent.find(prop => prop.name === 'revision').typeAnnotation.type,
  'Int32TypeAnnotation');
const stats = spec.events.find(event => event.name === 'onStats').typeAnnotation.argument.properties;
assert.equal(stats.find(prop => prop.name === 'loadedSplats').typeAnnotation.type,
  'DoubleTypeAnnotation');

execFileSync(process.execPath, [generate, '--platform', 'ios',
  '--schemaPath', schemaPath, '--outputDir', path.join(output, 'ios'),
  '--libraryName', codegenConfig.name, '--libraryType', codegenConfig.type], {stdio: 'inherit'});
// Android runs the script the Gradle build runs, so app autolinking's inputs are checked too.
execFileSync(process.execPath, [path.join(__dirname, 'codegen-android.cjs'), path.join(output, 'android')],
  {stdio: 'inherit'});
assert.ok(fs.existsSync(path.join(output, 'android/java/com/facebook/react/viewmanagers/SplatKitViewManagerInterface.java')));
assert.ok(fs.existsSync(path.join(output, 'android/jni/CMakeLists.txt')),
  'autolinking adds jni/ as the react_codegen_SplatKitSpec CMake target');
assert.ok(fs.existsSync(path.join(output, 'android/jni/react/renderer/components/SplatKitSpec/Props.h')));
assert.ok(fs.existsSync(path.join(output, 'ios/react/renderer/components/SplatKitSpec/Props.h')));
assert.ok(fs.existsSync(path.join(output, 'ios/react/renderer/components/SplatKitSpec/EventEmitters.h')));
console.log(JSON.stringify({status: 'passed', output, platforms: ['android', 'ios'],
  nativeAdaptersImplemented: {ios: true, android: true}, consumerAppValidated: false}));
