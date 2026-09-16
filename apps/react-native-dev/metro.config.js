const path = require('node:path');
const {getDefaultConfig, mergeConfig} = require('@react-native/metro-config');

// The local Fabric package this app exists to test. It lives outside node_modules and is
// pulled in via a `file:` dependency (symlinked by npm), so Metro must be told to watch its
// real location and to never resolve react/react-native from its own nested node_modules
// (installed there only so the package can typecheck/test standalone) - only from this app's.
const splatkitPackageRoot = path.resolve(__dirname, '../../packages/react-native-splatkit');

/**
 * Metro configuration
 * https://reactnative.dev/docs/metro
 *
 * @type {import('@react-native/metro-config').MetroConfig}
 */
const config = {
  watchFolders: [splatkitPackageRoot],
  resolver: {
    blockList: [new RegExp(`^${escapeRegex(path.join(splatkitPackageRoot, 'node_modules'))}/.*$`)],
    // Every import from the package, including Babel's injected @babel/runtime helpers.
    nodeModulesPaths: [path.resolve(__dirname, 'node_modules')],
  },
};

function escapeRegex(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

module.exports = mergeConfig(getDefaultConfig(__dirname), config);
