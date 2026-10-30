'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const {
  SplatKitBuilder,
  classifyPolicyChange,
  toNativeViewProps,
  toNativePolicyProp,
  nativeCapabilitiesFromEvent,
  QualityPreset,
} = require('../build/performance');
const {WorldKind} = require('../build/contracts');

const world = {
  requestId: 'world-1',
  filePath: '/documents/world.spz',
  maxShDegree: 3,
};
const capabilities = {
  limits: {
    maxLodCapacitySplats: 5_000_000,
    minResidencyCapacitySplats: 100_000,
    maxResidencyCapacitySplats: 5_000_000,
  },
  supportsHiZOcclusion: true,
  supportsSubgroups: true,
  maxTextureDimension: 16_384,
};

function build(configure = builder => builder) {
  return configure(new SplatKitBuilder().withWorld(world)).build(capabilities);
}

test('presets have real values and later manual options predictably override them', () => {
  const high = build(builder => builder.withPreset('high'));
  const manual = build(builder =>
    builder.withPreset('high').withPerformance({
      renderScale: 0.9,
      lodBudgetSplats: 1_500_000,
    }),
  );

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
  const config = build(builder =>
    builder.withPerformance({
      alphaThreshold: 0.5,
      renderScale: 1.1,
      shDegree: 2,
      lodBudgetSplats: 2_500_000,
      residencyCapacitySplats: 3_500_000,
      targetFps: 90,
    }),
  );

  assert.equal(config.performance.requested.alphaThreshold, 0.5);
  assert.equal(config.performance.requested.targetFps, 90);
  assert.deepEqual(config.performance.effective, {
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
  assert(
    config.performance.diagnostics.some(
      diagnostic =>
        diagnostic.option === 'targetFps' &&
        diagnostic.code === 'fabric-option-unavailable',
    ),
  );
  // Without native capabilities, changed policy fields say support is unknown, not inexpressible.
  assert(
    config.performance.diagnostics.some(
      diagnostic =>
        diagnostic.option === 'alphaThreshold' &&
        diagnostic.code === 'native-support-unknown',
    ),
  );
  assert(
    !config.performance.diagnostics.some(
      diagnostic => diagnostic.option === 'enableFrustumCulling',
    ),
  );
  assert.equal(
    new Set(
      config.performance.diagnostics.map(
        diagnostic => `${diagnostic.code}:${diagnostic.option}`,
      ),
    ).size,
    config.performance.diagnostics.length,
  );
});

test('16-bit depth keys are the native default, so only a 32-bit request warns before capabilities', () => {
  const warnsAboutSortDepth = sortDepth =>
    build(builder =>
      builder.withPerformance({sortDepth}),
    ).performance.diagnostics.some(
      diagnostic =>
        diagnostic.option === 'sortDepth' &&
        diagnostic.code === 'native-support-unknown',
    );
  assert.equal(warnsAboutSortDepth(16), false);
  assert.equal(warnsAboutSortDepth(32), true);
});

test('maps scale, SH and splat capacities to actual Fabric props', () => {
  const config = build(builder =>
    builder.withRender({paused: true}).withPerformance({
      renderScale: 0.75,
      shDegree: 1,
      lodBudgetSplats: 750_000,
      residencyCapacitySplats: 900_000,
    }),
  );

  assert.deepEqual(toNativeViewProps(config), {
    world: {
      ...world,
      kind: WorldKind.file,
      lodCapacitySplats: 750_000,
      residencyCapacitySplats: 900_000,
    },
    paused: true,
    renderScale: 0.75,
    shDegree: 1,
  });
});

test('a world is one file unless it names a tileset, which keeps its residency budget', () => {
  const tileset = new SplatKitBuilder()
    .withWorld({
      ...world,
      filePath: '/documents/city/tileset.json',
      kind: WorldKind.tileset,
    })
    .withPerformance({residencyCapacitySplats: 1_500_000})
    .build(capabilities);
  assert.equal(build().world.kind, WorldKind.file);
  assert.equal(tileset.world.kind, WorldKind.tileset);
  assert.equal(tileset.world.residencyCapacitySplats, 1_500_000);
  assert.equal(toNativeViewProps(tileset).world.kind, WorldKind.tileset);
  for (const kind of [2, -1, 'tileset', 0.5, null]) {
    assert.throws(
      () =>
        new SplatKitBuilder().withWorld({...world, kind}).build(capabilities),
      RangeError,
    );
  }
});

test('a world carries its lens to the native props and the build validates it', () => {
  const lens = {fieldOfViewDegrees: 30, nearPlane: 0.05, farPlane: 4};
  const config = new SplatKitBuilder()
    .withWorld({...world, ...lens})
    .build(capabilities);
  assert.deepEqual(
    {
      fieldOfViewDegrees: config.world.fieldOfViewDegrees,
      nearPlane: config.world.nearPlane,
      farPlane: config.world.farPlane,
    },
    lens,
  );
  assert.equal(toNativeViewProps(config).world.farPlane, lens.farPlane);
  assert.throws(
    () =>
      new SplatKitBuilder()
        .withWorld({...world, fieldOfViewDegrees: 5})
        .build(capabilities),
    RangeError,
  );
  assert.throws(
    () =>
      new SplatKitBuilder()
        .withWorld({...world, nearPlane: 0.05})
        .build(capabilities),
    TypeError,
  );
});

test('a world carries its source frame to the native props and the build validates it', () => {
  const {CoordinateFrame} = require('../build/contracts');
  assert.equal(build().world.sourceFrame, undefined);
  const config = new SplatKitBuilder()
    .withWorld({...world, sourceFrame: CoordinateFrame.rdf})
    .build(capabilities);
  assert.equal(config.world.sourceFrame, CoordinateFrame.rdf);
  assert.equal(
    toNativeViewProps(config).world.sourceFrame,
    CoordinateFrame.rdf,
  );
  for (const sourceFrame of ['rrr', 'ru', 'xyz']) {
    assert.throws(
      () =>
        new SplatKitBuilder()
          .withWorld({...world, sourceFrame})
          .build(capabilities),
      RangeError,
    );
  }
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
    kind: WorldKind.file,
    maxShDegree: 1,
    lodCapacitySplats: 800_000,
    residencyCapacitySplats: 900_000,
  });
  for (const option of [
    'lodBudgetSplats',
    'residencyCapacitySplats',
    'shDegree',
  ]) {
    assert(
      config.performance.diagnostics.some(
        diagnostic =>
          diagnostic.option === option && diagnostic.code === 'limit-clamped',
      ),
    );
  }
});

test('rejects malformed numeric, enum and duplicate-authority inputs', () => {
  const invalid = [
    {lodBudgetSplats: NaN},
    {lodBudgetSplats: 1.5},
    {residencyCapacitySplats: Infinity},
    {sortDepth: 24},
    {targetFps: 0},
    {enableHiZOcclusion: 1},
  ];
  for (const options of invalid) {
    assert.throws(() => new SplatKitBuilder().withPerformance(options));
  }
  assert.throws(() => new SplatKitBuilder().withPerformance({mystery: true}));
  assert.throws(() =>
    new SplatKitBuilder().withWorld({
      ...world,
      lodCapacitySplats: 10,
    }),
  );
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
  assert.throws(() => {
    config.performance.effective.renderScale = 2;
  });
});

test('classifies only operations represented by the Fabric contract', () => {
  const policy = build().performance.requested;
  assert.equal(classifyPolicyChange(policy, policy), 'none');
  assert.equal(
    classifyPolicyChange(policy, {...policy, renderScale: 0.75}),
    'nativePropUpdate',
  );
  assert.equal(
    classifyPolicyChange(policy, {...policy, lodBudgetSplats: 1_000_000}),
    'worldReload',
  );
  // Renderer policy fields travel in the versioned policy prop, not a world reload.
  assert.equal(
    classifyPolicyChange(policy, {...policy, alphaThreshold: 0.5}),
    'nativePropUpdate',
  );
  assert.equal(
    classifyPolicyChange(policy, {...policy, sortDepth: 32}),
    'nativePropUpdate',
  );
  assert.equal(
    classifyPolicyChange(policy, {...policy, targetFps: 60}),
    'unavailable',
  );
});

const nativeCapabilitiesEvent = Object.freeze({
  maxLodCapacitySplats: 2_200_000,
  minResidencyCapacitySplats: 100_000,
  maxResidencyCapacitySplats: 8_000_000,
  supportsHiZOcclusion: false,
  supportsSubgroups: true,
  maxTextureDimension: 16_384,
  policyLodErrorPixels: false,
  policyAlphaThreshold: false,
  policySubpixelThreshold: false,
  policyEnableFrustumCulling: false,
  policyEnableHiZOcclusion: false,
  policyEnableEarlyTermination: false,
  policySortDepth: true,
});
const nativeCapabilities = nativeCapabilitiesFromEvent(nativeCapabilitiesEvent);

// These capabilities leave the threshold to native, which warns unless the request is its default.
test('the builder default and every preset cut sub-pixel splats at one pixel', () => {
  assert.equal(build().performance.requested.subpixelThreshold, 1);
  for (const preset of Object.values(QualityPreset)) {
    assert.equal(
      build(builder => builder.withPreset(preset)).performance.requested
        .subpixelThreshold,
      1,
      preset,
    );
  }
});

test('a one-pixel sub-pixel threshold is the native default', () => {
  const warnsAboutThreshold = threshold =>
    new SplatKitBuilder()
      .withWorld(world)
      .withPerformance({subpixelThreshold: threshold})
      .build(nativeCapabilities)
      .performance.diagnostics.some(
        diagnostic => diagnostic.option === 'subpixelThreshold',
      );
  assert.equal(warnsAboutThreshold(1), false);
  assert.equal(warnsAboutThreshold(0.5), true);
});

test('native capabilities resolve the requested policy against what the adapter accepts', () => {
  const config = new SplatKitBuilder()
    .withWorld(world)
    .withPerformance({
      sortDepth: 16,
      subpixelThreshold: 0.75,
      alphaThreshold: 0.5,
    })
    .build(nativeCapabilities);

  // Supported fields reach the effective policy with their real values, not null.
  assert.equal(config.performance.effective.sortDepth, 16);
  // Supported-by-contract numeric budgets are unchanged.
  assert.equal(config.performance.effective.lodBudgetSplats, 2_000_000);
  // Unsupported fields stay null and warn once each with the native fallback code.
  assert.equal(config.performance.effective.alphaThreshold, null);
  assert.equal(config.performance.effective.subpixelThreshold, null);
  for (const option of ['alphaThreshold', 'subpixelThreshold']) {
    assert(
      config.performance.diagnostics.some(
        diagnostic =>
          diagnostic.option === option &&
          diagnostic.code === 'native-option-fallback',
      ),
    );
  }
});

test('builds the versioned native policy prop from the requested values', () => {
  const config = build(builder =>
    builder.withPerformance({
      sortDepth: 16,
      subpixelThreshold: 0.25,
    }),
  );
  assert.deepEqual(toNativePolicyProp(config, 7), {
    revision: 7,
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
    assert.throws(
      () => toNativePolicyProp(config, revision),
      RangeError,
      String(revision),
    );
  }
  assert.equal(toNativePolicyProp(config, 2 ** 31 - 1).revision, 2 ** 31 - 1);
});
