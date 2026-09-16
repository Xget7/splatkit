'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const {
  SplatKitBuilder,
  classifyPolicyChange,
  toNativeViewProps,
  toNativePolicyProp,
  nativeCapabilitiesFromEvent,
} = require('../build/performance');

const world = {requestId: 'world-1', filePath: '/tmp/world.spz', maxShDegree: 3};
const capabilities = {
  limits: {
    maxLodCapacitySplats: 5_000_000,
    minResidencyCapacitySplats: 100_000,
    maxResidencyCapacitySplats: 5_000_000,
  },
  supportsComputeTiles: true,
  supportsHiZOcclusion: true,
  supportsSubgroups: true,
  maxTextureDimension: 16_384,
};

function build(configure = builder => builder) {
  return configure(new SplatKitBuilder().withWorld(world)).build(capabilities);
}

test('presets have real values and later manual options predictably override them', () => {
  const high = build(builder => builder.withPreset('high'));
  const manual = build(builder => builder.withPreset('high').withPerformance({
    renderScale: 0.9,
    lodBudgetSplats: 1_500_000,
  }));

  assert.equal(high.performance.requested.mode, 'auto');
  assert.equal(high.performance.requested.renderScale, 1);
  assert.equal(high.performance.requested.shDegree, 3);
  assert.equal(high.performance.requested.lodBudgetSplats, 3_000_000);
  assert.equal(manual.performance.requested.mode, 'manual');
  assert.equal(manual.performance.requested.preset, 'high');
  assert.equal(manual.performance.requested.renderScale, 0.9);
  assert.equal(manual.performance.requested.lodBudgetSplats, 1_500_000);
  assert.equal(manual.performance.requested.shDegree, 3);
});

test('requested policy resolves to only values the current Fabric contract can apply', () => {
  const config = build(builder => builder.withPerformance({
    raster: 'computeTile',
    tileSize: 32,
    renderScale: 1.1,
    shDegree: 2,
    lodBudgetSplats: 2_500_000,
    residencyCapacitySplats: 3_500_000,
    targetFps: 90,
  }));

  assert.equal(config.performance.requested.raster, 'computeTile');
  assert.equal(config.performance.requested.targetFps, 90);
  assert.deepEqual(config.performance.effective, {
    raster: null,
    tileSize: null,
    lodErrorPixels: null,
    lodBudgetSplats: 2_500_000,
    alphaThreshold: null,
    subpixelThreshold: null,
    enableFrustumCulling: null,
    enableHiZOcclusion: null,
    enableEarlyTermination: null,
    sortDepth: null,
    renderScale: 1.1,
    shDegree: 2,
    residencyCapacitySplats: 3_500_000,
    targetFps: null,
  });
  assert(config.performance.diagnostics.some(diagnostic =>
    diagnostic.option === 'targetFps' && diagnostic.code === 'fabric-option-unavailable'));
  // Without native capabilities, changed policy fields say support is unknown, not inexpressible.
  assert(config.performance.diagnostics.some(diagnostic =>
    diagnostic.option === 'tileSize' && diagnostic.code === 'native-support-unknown'));
  assert(!config.performance.diagnostics.some(diagnostic =>
    diagnostic.option === 'enableFrustumCulling'));
  assert.equal(new Set(config.performance.diagnostics.map(diagnostic =>
    `${diagnostic.code}:${diagnostic.option}`)).size, config.performance.diagnostics.length);
});

test('maps scale, SH and splat capacities to actual Fabric props', () => {
  const config = build(builder => builder.withRender({paused: true}).withPerformance({
    renderScale: 0.75,
    shDegree: 1,
    lodBudgetSplats: 750_000,
    residencyCapacitySplats: 900_000,
  }));

  assert.deepEqual(toNativeViewProps(config), {
    world: {
      ...world,
      lodCapacitySplats: 750_000,
      residencyCapacitySplats: 900_000,
    },
    paused: true,
    renderScale: 0.75,
    shDegree: 1,
  });
});

test('clamps valid requests to supplied limits and reports each fallback', () => {
  const limited = {
    ...capabilities,
    limits: {
      maxLodCapacitySplats: 800_000,
      minResidencyCapacitySplats: 200_000,
      maxResidencyCapacitySplats: 900_000,
    },
  };
  const config = new SplatKitBuilder()
    .withWorld({...world, maxShDegree: 1})
    .withPreset('highEnd')
    .build(limited);

  assert.equal(config.performance.requested.lodBudgetSplats, 4_000_000);
  assert.equal(config.performance.effective.lodBudgetSplats, 800_000);
  assert.equal(config.performance.effective.residencyCapacitySplats, 900_000);
  assert.equal(config.performance.effective.shDegree, 1);
  assert.deepEqual(config.world, {
    ...world,
    maxShDegree: 1,
    lodCapacitySplats: 800_000,
    residencyCapacitySplats: 900_000,
  });
  for (const option of ['lodBudgetSplats', 'residencyCapacitySplats', 'shDegree']) {
    assert(config.performance.diagnostics.some(diagnostic =>
      diagnostic.option === option && diagnostic.code === 'limit-clamped'));
  }
});

test('hardware-only snapshot suggests but does not claim a raster fallback', () => {
  const config = new SplatKitBuilder().withWorld(world).withPerformance({
    raster: 'computeTile',
  }).build({...capabilities, supportsComputeTiles: false});
  const diagnostic = config.performance.diagnostics.find(item => item.option === 'raster');

  assert.equal(config.performance.effective.raster, null);
  assert.equal(diagnostic.code, 'capability-fallback-unavailable');
  assert.equal(diagnostic.fallback, 'hardware');
});

test('rejects malformed numeric, enum and duplicate-authority inputs', () => {
  const invalid = [
    {lodBudgetSplats: NaN},
    {lodBudgetSplats: 1.5},
    {residencyCapacitySplats: Infinity},
    {tileSize: 12},
    {sortDepth: 24},
    {raster: 'magic'},
    {targetFps: 0},
    {enableHiZOcclusion: 1},
  ];
  for (const options of invalid) {
    assert.throws(() => new SplatKitBuilder().withPerformance(options));
  }
  assert.throws(() => new SplatKitBuilder().withPerformance({mystery: true}));
  assert.throws(() => new SplatKitBuilder().withWorld({
    ...world,
    lodCapacitySplats: 10,
  }));
  assert.throws(() => new SplatKitBuilder().withRender({renderScale: 1}));
  assert.throws(() => new SplatKitBuilder().withPreset('ultra'));
  assert.throws(() => new SplatKitBuilder().withPreset('constructor'));
});

test('returns immutable snapshots isolated from caller mutation', () => {
  const input = {...world};
  const config = new SplatKitBuilder().withWorld(input).build(capabilities);
  input.requestId = 'changed';

  assert.equal(config.world.requestId, 'world-1');
  assert(Object.isFrozen(config));
  assert(Object.isFrozen(config.world));
  assert(Object.isFrozen(config.render));
  assert(Object.isFrozen(config.performance));
  assert(Object.isFrozen(config.performance.requested));
  assert(Object.isFrozen(config.performance.effective));
  assert(Object.isFrozen(config.performance.diagnostics));
  assert.throws(() => { config.performance.effective.renderScale = 2; });
});

test('classifies only operations represented by the Fabric contract', () => {
  const policy = build().performance.requested;
  assert.equal(classifyPolicyChange(policy, policy), 'none');
  assert.equal(classifyPolicyChange(policy, {...policy, renderScale: 0.75}),
    'nativePropUpdate');
  assert.equal(classifyPolicyChange(policy, {...policy, lodBudgetSplats: 1_000_000}),
    'worldReload');
  // Renderer policy fields travel in the versioned policy prop, not a world reload.
  assert.equal(classifyPolicyChange(policy, {...policy, raster: 'hardware'}),
    'nativePropUpdate');
  assert.equal(classifyPolicyChange(policy, {...policy, sortDepth: 32}), 'nativePropUpdate');
  assert.equal(classifyPolicyChange(policy, {...policy, targetFps: 60}), 'unavailable');
});

const nativeCapabilities = nativeCapabilitiesFromEvent({
  maxLodCapacitySplats: 2_200_000,
  minResidencyCapacitySplats: 100_000,
  maxResidencyCapacitySplats: 8_000_000,
  supportsComputeTiles: false,
  supportsHiZOcclusion: false,
  supportsSubgroups: true,
  maxTextureDimension: 16_384,
  policyRaster: false,
  policyTileSize: false,
  policyLodErrorPixels: false,
  policyAlphaThreshold: false,
  policySubpixelThreshold: true,
  policyEnableFrustumCulling: false,
  policyEnableHiZOcclusion: false,
  policyEnableEarlyTermination: false,
  policySortDepth: true,
});

test('native capabilities resolve the requested policy against what the adapter accepts', () => {
  const config = new SplatKitBuilder().withWorld(world).withPerformance({
    raster: 'hybrid',
    sortDepth: 16,
    subpixelThreshold: 0.75,
    alphaThreshold: 0.5,
  }).build(nativeCapabilities);

  // Supported fields reach the effective policy with their real values, not null.
  assert.equal(config.performance.effective.sortDepth, 16);
  assert.equal(config.performance.effective.subpixelThreshold, 0.75);
  // Supported-by-contract numeric budgets are unchanged.
  assert.equal(config.performance.effective.lodBudgetSplats, 2_000_000);
  // Unsupported fields stay null and warn once each with the native fallback code.
  assert.equal(config.performance.effective.raster, null);
  assert.equal(config.performance.effective.alphaThreshold, null);
  for (const option of ['raster', 'alphaThreshold']) {
    assert(config.performance.diagnostics.some(diagnostic =>
      diagnostic.option === option && diagnostic.code === 'native-option-fallback'));
  }
});

test('builds the versioned native policy prop from the requested values', () => {
  const config = build(builder => builder.withPerformance({
    raster: 'computeTile',
    sortDepth: 16,
    subpixelThreshold: 0.25,
  }));
  assert.deepEqual(toNativePolicyProp(config, 7), {
    revision: 7,
    raster: 1,
    tileSize: 16,
    lodErrorPixels: 1.25,
    alphaThreshold: 1 / 255,
    subpixelThreshold: 0.25,
    enableFrustumCulling: true,
    enableHiZOcclusion: false,
    enableEarlyTermination: true,
    sortDepth: 16,
  });
});

test('the policy revision is a positive Int32, because native treats 0 or less as no policy', () => {
  const config = build();
  for (const revision of [0, -1, 1.5, Number.NaN, 2 ** 31]) {
    assert.throws(() => toNativePolicyProp(config, revision), RangeError, String(revision));
  }
  assert.equal(toNativePolicyProp(config, 2 ** 31 - 1).revision, 2 ** 31 - 1);
});
