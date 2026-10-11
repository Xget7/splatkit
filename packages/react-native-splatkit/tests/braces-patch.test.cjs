'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const test = require('node:test');
const { patch } = require('../scripts/patch-braces.cjs');
const backport = require('../scripts/patches/braces-3.0.3/backport.cjs');

const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const installed = path.dirname(require.resolve('braces/package.json'));
const sources = Object.fromEntries(Object.entries(backport.originalSha256).map(([file, hash]) => {
  let bytes = fs.readFileSync(path.join(installed, file));
  if (sha256(bytes) !== hash) {
    let text = bytes.toString('utf8');
    for (const [before, after] of [...(backport.replacements[file] || [])].reverse()) {
      text = text.replace(after, before);
    }
    bytes = Buffer.from(text);
  }
  assert.equal(sha256(bytes), hash, `Fixture must restore the published ${file}`);
  return [file, bytes];
}));

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'splatkit-braces-patch-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  return root;
}

function install(directory) {
  fs.mkdirSync(directory, { recursive: true });
  fs.writeFileSync(path.join(directory, 'package.json'), JSON.stringify({ name: 'braces', version: '3.0.3' }));
  for (const [file, bytes] of Object.entries(sources)) {
    const target = path.join(directory, file);
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.writeFileSync(target, bytes);
  }
  return directory;
}

function snapshot(directory) {
  return fs.readdirSync(directory, { recursive: true }).filter(file =>
    fs.lstatSync(path.join(directory, file)).isFile()).sort().map(file =>
    [file, sha256(fs.readFileSync(path.join(directory, file)))]);
}

const patchError = error => error.code === 'ERR_BRACES_PATCH';

test('patches real nested copies and verifies idempotently without rewriting files', t => {
  const root = fixture(t);
  const modules = path.join(root, 'node_modules');
  install(path.join(modules, 'braces'));
  install(path.join(modules, '@scope/host/node_modules/braces'));
  assert.throws(() => patch({ nodeModules: [modules], check: true }), patchError);
  const result = patch({ nodeModules: [modules] });
  assert.equal(result.copies.length, 2);
  assert.ok(result.copies.every(copy => !copy.alreadyPatched));
  const protectedFiles = ['lib/compile.js', 'lib/validate-depth.js'].map(file => path.join(modules, 'braces', file));
  const fixedTime = new Date('2001-01-01T00:00:00Z');
  for (const file of protectedFiles) fs.utimesSync(file, fixedTime, fixedTime);
  const before = snapshot(modules);
  const verified = patch({ nodeModules: [modules], check: true });
  assert.equal(verified.status, 'verified');
  assert.ok(verified.copies.every(copy => copy.alreadyPatched));
  assert.ok(patch({ nodeModules: [modules] }).copies.every(copy => copy.alreadyPatched));
  assert.deepEqual(snapshot(modules), before);
  for (const file of protectedFiles) assert.equal(fs.statSync(file).mtimeMs, fixedTime.getTime());
});

test('changed upstream prevents mutation of every otherwise-valid copy', t => {
  const root = fixture(t);
  const modules = path.join(root, 'node_modules');
  const first = install(path.join(modules, 'braces'));
  const secondModules = path.join(root, 'other/node_modules');
  const second = install(path.join(secondModules, 'braces'));
  fs.appendFileSync(path.join(second, 'lib/parse.js'), '\n// upstream changed\n');
  const before = snapshot(first);
  assert.throws(() => patch({ nodeModules: [modules, secondModules] }), patchError);
  assert.deepEqual(snapshot(first), before);
  assert.equal(fs.existsSync(path.join(first, 'lib/validate-depth.js')), false);
});

test('rejects unsupported versions and damaged patched guards', t => {
  const root = fixture(t);
  const modules = path.join(root, 'node_modules');
  const braces = install(path.join(modules, 'braces'));
  fs.writeFileSync(path.join(braces, 'package.json'), JSON.stringify({ name: 'braces', version: '3.0.4' }));
  assert.throws(() => patch({ nodeModules: [modules] }), patchError);
  fs.writeFileSync(path.join(braces, 'package.json'), JSON.stringify({ name: 'braces', version: '3.0.3' }));
  patch({ nodeModules: [modules] });
  fs.appendFileSync(path.join(braces, 'lib/validate-depth.js'), '\n// altered guard\n');
  assert.throws(() => patch({ nodeModules: [modules], check: true }), patchError);
  assert.throws(() => patch({ nodeModules: [modules] }), patchError);
});

test('ignores linked packages and nested node_modules belonging to another project', t => {
  const root = fixture(t);
  const modules = path.join(root, 'node_modules');
  install(path.join(modules, 'braces'));
  const outside = path.join(root, 'other-project');
  install(path.join(outside, 'node_modules/braces'));
  const before = snapshot(outside);
  fs.symlinkSync(outside, path.join(modules, 'linked-host'), 'dir');
  fs.mkdirSync(path.join(modules, 'host'));
  fs.symlinkSync(path.join(outside, 'node_modules'), path.join(modules, 'host/node_modules'), 'dir');
  assert.equal(patch({ nodeModules: [modules] }).copies.length, 1);
  assert.deepEqual(snapshot(outside), before);
});

test('rejects source-directory symlinks before changing either installation', t => {
  const root = fixture(t);
  const modules = path.join(root, 'node_modules');
  const braces = install(path.join(modules, 'braces'));
  const outside = install(path.join(root, 'other-project/node_modules/braces'));
  fs.rmSync(path.join(braces, 'lib'), { recursive: true });
  fs.symlinkSync(path.join(outside, 'lib'), path.join(braces, 'lib'), 'dir');
  const before = snapshot(outside);
  assert.throws(() => patch({ nodeModules: [modules] }), patchError);
  assert.deepEqual(snapshot(outside), before);
});

test('optional prepare permits absent tooling while ordinary patch and check stay strict', t => {
  const root = fixture(t);
  const missing = path.join(root, 'missing/node_modules');
  const empty = path.join(root, 'empty/node_modules');
  fs.mkdirSync(empty, { recursive: true });
  for (const modules of [missing, empty]) {
    assert.equal(patch({ nodeModules: [modules], ifPresent: true }).status, 'absent');
    assert.throws(() => patch({ nodeModules: [modules] }), patchError);
    assert.throws(() => patch({ nodeModules: [modules], ifPresent: true, check: true }), patchError);
  }
  const modules = path.join(root, 'present/node_modules');
  install(path.join(modules, 'braces'));
  assert.equal(patch({ nodeModules: [missing, modules], ifPresent: true }).copies.length, 1);
});

test('CLI exposes optional installation without weakening --check', t => {
  const root = fixture(t);
  const modules = path.join(root, 'missing/node_modules');
  const script = path.resolve(__dirname, '../scripts/patch-braces.cjs');
  const optional = spawnSync(process.execPath, [script, '--node-modules', modules, '--if-present'], { encoding: 'utf8' });
  assert.equal(optional.status, 0, optional.stderr);
  assert.equal(JSON.parse(optional.stdout).status, 'absent');
  const check = spawnSync(process.execPath, [script, '--node-modules', modules, '--if-present', '--check'], { encoding: 'utf8' });
  assert.equal(check.status, 1);
  assert.match(check.stderr, /ERR_BRACES_PATCH/);
});
