const path = require('node:path');
const { getDefaultConfig, mergeConfig } = require('@react-native/metro-config');

// This example lives in the SplatKit monorepo and installs the package from ../../packages,
// so npm links it instead of unpacking it. Metro only follows a symlink out of the project
// when the target is a watch folder.
const sdk = path.resolve(__dirname, '../../packages/react-native-splatkit');
const modules = path.resolve(__dirname, 'node_modules');

// The SDK keeps React and React Native as devDependencies, so from its own directory Metro
// resolves them to its node_modules and the bundle ends up with two copies. Two copies means
// two view config registries: the SDK registers SplatKitView in its own and the renderer, on
// the app's copy, then renders a component it has never heard of. Pin both to this app.
const single = new Set(['react', 'react-native']);
// Resolution starts at the requesting file and walks up, so a request answered as if it came
// from this file lands in the app's node_modules wherever it was made. Rewriting the request
// to an absolute path would do the same but skip the package's own exports map, which is how
// react-native/asset-registry and every other subpath import finds its file.
const here = path.join(__dirname, 'index.js');

/**
 * Metro configuration
 * https://reactnative.dev/docs/metro
 *
 * @type {import('@react-native/metro-config').MetroConfig}
 */
const config = {
  watchFolders: [sdk],
  resolver: {
    nodeModulesPaths: [modules],
    resolveRequest: (context, moduleName, platform) => {
      const scope = moduleName.split('/')[0];
      if (single.has(scope)) {
        return context.resolveRequest(
          { ...context, originModulePath: here },
          moduleName,
          platform,
        );
      }
      return context.resolveRequest(context, moduleName, platform);
    },
  },
};

module.exports = mergeConfig(getDefaultConfig(__dirname), config);
