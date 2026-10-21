import js from '@eslint/js';
import prettier from 'eslint-config-prettier/flat';
import sonarjs from 'eslint-plugin-sonarjs';
import {defineConfig} from 'eslint/config';
import globals from 'globals';
import tseslint from 'typescript-eslint';

export default defineConfig([
  // scripts/patches holds third-party sources backported verbatim.
  {ignores: ['**/build/', 'scripts/patches/']},
  js.configs.recommended,
  sonarjs.configs.recommended,
  {
    files: ['**/*.ts'],
    extends: [tseslint.configs.strictTypeChecked],
    languageOptions: {
      parserOptions: {
        projectService: true,
        tsconfigRootDir: import.meta.dirname,
      },
    },
  },
  {
    files: ['**/*.cjs', '**/*.js'],
    languageOptions: {sourceType: 'commonjs', globals: globals.node},
  },
  prettier,
]);
