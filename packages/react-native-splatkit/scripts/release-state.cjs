'use strict';

const fs = require('node:fs');
const {spawnSync} = require('node:child_process');
const semver = require('semver');

function readRegistry(name, run = spawnSync) {
  const result = run('npm', ['view', name, 'versions', 'dist-tags', '--json'],
    {encoding: 'utf8', timeout: 60000});
  if (result.error) throw result.error;
  let metadata;
  try {
    metadata = JSON.parse(result.stdout);
  } catch {
    throw new Error(`Cannot read npm metadata for ${name}; refusing to publish`);
  }
  if (result.status !== 0) {
    // Only an explicit registry 404 means a first publication. Auth, network and server
    // failures must not turn an ordinary push into an attempted release.
    if (metadata.error?.code === 'E404') return {versions: [], 'dist-tags': {}};
    throw new Error(`npm metadata query failed (${metadata.error?.code ?? result.status})`);
  }
  return metadata;
}

function releaseState({name, version}, metadata) {
  if (!semver.valid(version)) throw new Error(`Invalid package version: ${version}`);
  const versions = typeof metadata.versions === 'string' ? [metadata.versions] : metadata.versions;
  const tags = metadata['dist-tags'];
  const latest = tags?.latest;
  if (!Array.isArray(versions) || versions.some(value => !semver.valid(value)) ||
      !tags || typeof tags !== 'object' || Array.isArray(tags) ||
      (latest !== undefined && !semver.valid(latest))) {
    throw new Error('Invalid npm version metadata; refusing to publish');
  }
  const isNew = !versions.includes(version);
  const prerelease = semver.prerelease(version) !== null;
  if (isNew && !prerelease && latest && semver.lte(version, latest)) {
    throw new Error(`Stable ${version} does not advance latest (${latest})`);
  }
  return {
    name, version, new: isNew, tag: prerelease ? 'next' : 'latest',
    // Retry only promotes forward, and a stable latest is never replaced by a beta.
    promoteLatest: !latest || (semver.prerelease(latest) !== null && semver.gt(version, latest)),
  };
}

function main() {
  const state = releaseState(require('../package.json'), readRegistry(require('../package.json').name));
  if (process.env.GITHUB_OUTPUT) {
    fs.appendFileSync(process.env.GITHUB_OUTPUT,
      Object.entries(state).map(([key, value]) => `${key}=${value}\n`).join(''));
  }
  console.log(JSON.stringify(state));
}

if (require.main === module) {
  try { main(); } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}

module.exports = {readRegistry, releaseState};
