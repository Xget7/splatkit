# Contributing

This repository is exported from `packages/react-native-splatkit` in [SplatKit](https://github.com/Xget7/splatkit-android).
Open changes there; `python3 scripts/export-ios-source.py --platform react-native` regenerates this tree and its `source-manifest.json`.
Run `npm ci` and `npm run check` for TypeScript, contract tests and Fabric Codegen.
Native adapter changes also need the SplatKit repository's adapter host tests and the RN dev app.
Report skipped checks and emulator or physical device provenance.
Keep docs short; contracts belong beside code.
