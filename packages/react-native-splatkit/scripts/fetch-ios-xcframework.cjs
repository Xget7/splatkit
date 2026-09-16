// Fetches SplatKitCore.xcframework into ios/ at npm prepack (npm pack / npm publish), so the
// pod has something to vendor. CocoaPods' `prepare_command` does not run for `:path` pods, which
// is how React Native autolinking installs a package from node_modules, so the framework has to
// already be on disk by the time `pod install` reads the podspec; it is never committed to git
// (see .gitignore), both because it is a binary build artifact and because it changes with every
// splatkit-ios release.
//
// SPLATKIT_IOS_XCFRAMEWORK_PATH overrides the default GitHub release download with a local
// build (a `.xcframework` directory or a `SplatKitCore.xcframework.zip`, both produced by
// scripts/package-ios.sh in the splatkit-ios repository), for developing against SDK changes
// that have not been released yet. That path is trusted as-is and skips checksum verification.
'use strict';
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {execFileSync} = require('node:child_process');

const root = path.resolve(__dirname, '..');
const destination = path.join(root, 'ios', 'SplatKitCore.xcframework');

function unzip(zipPath, destDir) {
  fs.mkdirSync(destDir, {recursive: true});
  execFileSync('unzip', ['-q', '-o', zipPath, '-d', destDir], {stdio: 'inherit'});
}

/** Moves the sole `*.xcframework` found directly under `dir` to `destination`. */
function installXcframeworkFrom(dir) {
  const found = fs.readdirSync(dir).find(name => name.endsWith('.xcframework'));
  if (!found) throw new Error(`no *.xcframework found in ${dir}`);
  fs.rmSync(destination, {recursive: true, force: true});
  fs.renameSync(path.join(dir, found), destination);
}

function fromLocalOverride(overridePath) {
  const resolved = path.resolve(overridePath);
  if (!fs.existsSync(resolved)) throw new Error(`SPLATKIT_IOS_XCFRAMEWORK_PATH not found: ${resolved}`);
  console.warn(`fetch-ios-xcframework: using local override ${resolved} (checksum not verified)`);
  if (resolved.endsWith('.xcframework')) {
    fs.rmSync(destination, {recursive: true, force: true});
    fs.cpSync(resolved, destination, {recursive: true});
    return {source: 'local-directory', path: resolved};
  }
  const staging = fs.mkdtempSync(path.join(os.tmpdir(), 'splatkit-ios-xcframework-'));
  unzip(resolved, staging);
  installXcframeworkFrom(staging);
  fs.rmSync(staging, {recursive: true, force: true});
  return {source: 'local-zip', path: resolved};
}

async function fromRelease() {
  const {version, sha256} = JSON.parse(fs.readFileSync(path.join(__dirname, 'ios-xcframework.json'), 'utf8'));
  if (!sha256) {
    throw new Error(
      `scripts/ios-xcframework.json has no sha256 for splatkit-ios v${version}: that release has not ` +
      'been cut yet. Fill it in (swift package compute-checksum, from scripts/package-ios.sh) after ' +
      'cutting it, or set SPLATKIT_IOS_XCFRAMEWORK_PATH to a local build for development.'
    );
  }
  const url = `https://github.com/Xget7/splatkit-ios/releases/download/v${version}/SplatKitCore.xcframework.zip`;
  console.warn(`fetch-ios-xcframework: downloading ${url}`);
  const response = await fetch(url);
  if (!response.ok) throw new Error(`fetch-ios-xcframework: ${url} responded ${response.status}`);
  const bytes = Buffer.from(await response.arrayBuffer());
  const actual = crypto.createHash('sha256').update(bytes).digest('hex');
  if (actual !== sha256) {
    throw new Error(`fetch-ios-xcframework: checksum mismatch for ${url}\nexpected ${sha256}\nactual   ${actual}`);
  }
  const staging = fs.mkdtempSync(path.join(os.tmpdir(), 'splatkit-ios-xcframework-'));
  const zipPath = path.join(staging, 'SplatKitCore.xcframework.zip');
  fs.writeFileSync(zipPath, bytes);
  unzip(zipPath, staging);
  installXcframeworkFrom(staging);
  fs.rmSync(staging, {recursive: true, force: true});
  return {source: 'release', url, sha256};
}

async function main() {
  const override = process.env.SPLATKIT_IOS_XCFRAMEWORK_PATH;
  const result = override ? fromLocalOverride(override) : await fromRelease();
  const infoPlist = path.join(destination, 'Info.plist');
  if (!fs.existsSync(infoPlist)) throw new Error(`fetch-ios-xcframework: ${infoPlist} missing after install`);
  console.log(JSON.stringify({status: 'installed', destination, ...result}));
}

main().catch(error => {
  console.error(error.message);
  process.exit(1);
});
