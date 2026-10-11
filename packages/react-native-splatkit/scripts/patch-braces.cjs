'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const backport = require('./patches/braces-3.0.3/backport.cjs');

const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const patchDirectory = path.join(__dirname, 'patches/braces-3.0.3');
const guardPath = 'lib/validate-depth.js';
const guardSource = fs.readFileSync(path.join(patchDirectory, 'validate-depth.js'));

const fail = message => {
  const error = new Error(message);
  error.code = 'ERR_BRACES_PATCH';
  throw error;
};

function installedBraces(nodeModules, ifPresent) {
  const found = [];
  const visited = new Set();

  const visit = directory => {
    const canonical = fs.realpathSync(directory);
    if (visited.has(canonical)) return;
    visited.add(canonical);
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
      // Never follow file: links or touch another project's installation.
      if (!entry.isDirectory() || entry.name.startsWith('.')) continue;
      const absolute = path.join(directory, entry.name);
      if (entry.name.startsWith('@')) {
        for (const scoped of fs.readdirSync(absolute, { withFileTypes: true })) {
          if (scoped.isDirectory()) visitPackage(path.join(absolute, scoped.name));
        }
      } else {
        visitPackage(absolute);
      }
    }
  };

  const visitPackage = directory => {
    if (path.basename(directory) === 'braces') found.push(directory);
    const nested = path.join(directory, 'node_modules');
    if (fs.existsSync(nested) && fs.lstatSync(nested).isDirectory()) visit(nested);
  };

  for (const root of nodeModules) {
    if (!fs.existsSync(root) && ifPresent) continue;
    if (!fs.existsSync(root) || !fs.lstatSync(root).isDirectory()) {
      fail(`Expected an explicit node_modules directory: ${root}`);
    }
    visit(root);
  }
  if (found.length === 0 && !ifPresent) {
    fail('No real braces installation found in the supplied node_modules directories');
  }
  return [...new Set(found.map(directory => fs.realpathSync(directory)))];
}

function patchedSource(relative, original) {
  let source = original.toString('utf8');
  for (const [before, after] of backport.replacements[relative] || []) {
    if (source.split(before).length !== 2) fail(`Patch anchor changed: ${relative}`);
    source = source.replace(before, after);
  }
  return Buffer.from(source);
}

function inspect(directory) {
  const manifestFile = path.join(directory, 'package.json');
  if (!fs.lstatSync(manifestFile).isFile() || fs.realpathSync(manifestFile) !== manifestFile) {
    fail(`Expected a real package manifest: ${manifestFile}`);
  }
  const manifest = JSON.parse(fs.readFileSync(manifestFile, 'utf8'));
  if (manifest.name !== 'braces' || manifest.version !== backport.version) {
    fail(`Unsupported braces installation at ${directory}: ${manifest.name}@${manifest.version}`);
  }

  const states = [];
  const writes = [];
  for (const [relative, originalHash] of Object.entries(backport.originalSha256)) {
    const file = path.join(directory, relative);
    if (!fs.existsSync(file) || !fs.lstatSync(file).isFile() || fs.realpathSync(file) !== file) {
      fail(`Expected a real source file: ${file}`);
    }
    const source = fs.readFileSync(file);
    const actualHash = sha256(source);
    if (actualHash === originalHash) {
      if (backport.replacements[relative]) {
        states.push('original');
        writes.push({ file, bytes: patchedSource(relative, source) });
      }
      continue;
    }
    // Reconstruct the original using only the exact known patch anchors.
    // The hash then establishes provenance and makes reapplication idempotent.
    if (!backport.replacements[relative]) fail(`Unexpected upstream source hash: ${file}`);
    let original = source.toString('utf8');
    for (const [before, after] of [...backport.replacements[relative]].reverse()) {
      if (original.split(after).length !== 2) fail(`Unexpected patched source: ${file}`);
      original = original.replace(after, before);
    }
    if (sha256(Buffer.from(original)) !== originalHash ||
        !patchedSource(relative, Buffer.from(original)).equals(source)) {
      fail(`Unexpected upstream source hash: ${file}`);
    }
    states.push('patched');
  }

  const guard = path.join(directory, guardPath);
  if (fs.realpathSync(path.dirname(guard)) !== path.dirname(guard)) {
    fail(`Expected a real source directory: ${path.dirname(guard)}`);
  }
  if (fs.existsSync(guard)) {
    if (!fs.lstatSync(guard).isFile() || !fs.readFileSync(guard).equals(guardSource)) {
      fail(`Unexpected depth guard: ${guard}`);
    }
  } else {
    writes.unshift({ file: guard, bytes: guardSource });
  }
  const protectedAlready = states.every(state => state === 'patched') && fs.existsSync(guard);
  return { directory, protectedAlready, writes };
}

function patch({ nodeModules, check = false, ifPresent = false }) {
  if (!Array.isArray(nodeModules) || nodeModules.length === 0) {
    fail('Supply at least one explicit nodeModules directory');
  }
  // Validate every copy before writing any source, including nested installs.
  const copies = installedBraces(nodeModules.map(root => path.resolve(root)), ifPresent && !check).map(inspect);
  if (copies.length === 0) return { status: 'absent', version: backport.version, copies: [] };
  if (check && copies.some(copy => !copy.protectedAlready)) {
    fail('braces depth backport is missing; run patch-braces.cjs without --check');
  }
  if (!check) {
    for (const copy of copies) {
      for (const write of copy.writes) fs.writeFileSync(write.file, write.bytes);
    }
  }
  return { status: check ? 'verified' : 'patched', version: backport.version,
    copies: copies.map(copy => ({ path: copy.directory, alreadyPatched: copy.protectedAlready })) };
}

if (require.main === module) {
  try {
    const nodeModules = [];
    let check = false;
    let ifPresent = false;
    for (let index = 2; index < process.argv.length; index++) {
      const argument = process.argv[index];
      if (argument === '--check') {
        check = true;
      } else if (argument === '--if-present') {
        ifPresent = true;
      } else if (argument === '--node-modules' && process.argv[index + 1]) {
        nodeModules.push(process.argv[++index]);
      } else {
        fail(`Unknown argument: ${argument}. Use --node-modules <directory> [--check] [--if-present]`);
      }
    }
    console.log(JSON.stringify(patch({ nodeModules, check, ifPresent })));
  } catch (error) {
    console.error(`${error.code || 'ERR_BRACES_PATCH'}: ${error.message}`);
    process.exitCode = 1;
  }
}

module.exports = { patch };
