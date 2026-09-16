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
