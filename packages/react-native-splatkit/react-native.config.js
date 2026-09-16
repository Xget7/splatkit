/** @type {import('@react-native-community/cli-types').Config} */
module.exports = {
  dependency: {
    // iOS autolinking finds SplatKitReactNative.podspec at the package root.
    platforms: {android: {sourceDir: 'android'}},
  },
};
