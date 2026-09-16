const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const root = path.resolve(__dirname, '..');
const output = path.resolve(process.argv[2]);
fs.mkdirSync(output, {recursive: true});
const schema = path.join(output, 'schema.json');
execFileSync(process.execPath, [require.resolve('@react-native/codegen/lib/cli/combine/combine-js-to-schema-cli.js'),
  schema, path.join(root, 'src/specs')], {stdio: 'inherit'});
// No --libraryType, like the RN Gradle plugin: only the modules generator writes the
// jni/CMakeLists.txt that app autolinking adds as the react_codegen_SplatKitSpec target.
execFileSync(process.execPath, [require.resolve('react-native/scripts/generate-specs-cli'),
  '--platform', 'android', '--schemaPath', schema, '--outputDir', output,
  '--libraryName', 'SplatKitSpec', '--javaPackageName', 'com.splatkit.reactnative'],
  {stdio: 'inherit'});
