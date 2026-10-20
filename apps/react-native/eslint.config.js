const reactNative = require('@react-native/eslint-config/flat');
const sonarjs = require('eslint-plugin-sonarjs');
const {defineConfig} = require('eslint/config');
const tseslint = require('typescript-eslint');

module.exports = defineConfig([
  {ignores: ['android/', 'ios/', 'build/', 'vendor/']},
  reactNative,
  sonarjs.configs.recommended,
  {
    files: ['**/*.ts', '**/*.tsx'],
    extends: [tseslint.configs.strictTypeChecked],
    languageOptions: {
      parserOptions: {projectService: true, tsconfigRootDir: __dirname},
    },
  },
]);
