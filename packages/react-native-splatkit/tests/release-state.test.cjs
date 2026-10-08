'use strict';

const assert = require('node:assert/strict');
const {test} = require('node:test');
const {readRegistry, releaseState} = require('../scripts/release-state.cjs');

const name = '@splatkit/react-native';
const metadata = (versions, latest) => ({versions, 'dist-tags': latest ? {latest} : {}});
const state = (version, registry) => releaseState({name, version}, registry);

test('registry failures cannot trigger a publication', () => {
  for (const code of ['E401', 'E403', 'E500', 'ETIMEDOUT']) {
    assert.throws(() => readRegistry(name, () => ({status: 1,
      stdout: JSON.stringify({error: {code}})})), /query failed/);
  }
  assert.throws(() => readRegistry(name, () => ({status: 1, stdout: 'unavailable'})), /refusing to publish/);
  assert.throws(() => readRegistry(name, () => ({error: new Error('network')})), /network/);
  assert.deepEqual(readRegistry(name, () => ({status: 1,
    stdout: JSON.stringify({error: {code: 'E404'}})})), metadata([]));
});

test('a new beta publishes next and advances a prerelease latest', () => {
  assert.deepEqual(state('0.1.0-beta.10', metadata(['0.1.0-beta.9'], '0.1.0-beta.9')),
    {name, version: '0.1.0-beta.10', new: true, tag: 'next', promoteLatest: true});
  assert.equal(state('0.1.0-beta.1', metadata([])).new, true);
});

test('retries of an old published beta cannot move latest backwards', () => {
  const retry = state('0.1.0-beta.1', metadata(['0.1.0-beta.1', '0.1.0-beta.2'], '0.1.0-beta.2'));
  assert.equal(retry.new, false);
  assert.equal(retry.promoteLatest, false);
});

test('a stable latest survives newer beta publications', () => {
  assert.equal(state('0.2.0-beta.1', metadata(['0.1.0'], '0.1.0')).promoteLatest, false);
  assert.equal(state('0.1.0', metadata(['0.1.0'], '0.1.0')).new, false);
  assert.equal(state('0.2.0', metadata(['0.1.0'], '0.1.0')).tag, 'latest');
  assert.throws(() => state('0.0.9', metadata(['0.1.0'], '0.1.0')), /does not advance latest/);
});

test('a retry repairs a missing latest without publishing the version again', () => {
  const retry = state('0.1.0-beta.1', metadata('0.1.0-beta.1'));
  assert.equal(retry.new, false);
  assert.equal(retry.promoteLatest, true);
});

test('malformed registry versions are rejected before any release action', () => {
  for (const registry of [{}, {versions: []}, metadata(['broken']), metadata([], 'broken')]) {
    assert.throws(() => state('0.1.0', registry), /Invalid npm version metadata/);
  }
  assert.throws(() => state('broken', metadata([])), /Invalid package version/);
});
