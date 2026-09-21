# Contributing

Thanks for improving SplatKit's React Native package.

## Where changes go

This repository is generated from `packages/react-native-splatkit` in the [SplatKit](https://github.com/Xget7/splatkit) monorepo and mirrored here on every merge.
Do not open a pull request against this tree: it is overwritten by the next mirror run, so any edit made here is lost.
Open the change upstream in SplatKit, where the package, its native adapters, the host tests and the example app all live together.
From a SplatKit checkout, `python3 scripts/export-ios-source.py --platform react-native` reproduces this tree and its `source-manifest.json`.
The manifest carries a SHA-256 for every published file, so it shows whether a local copy still matches the export.

## What this package is

`@splatkit/react-native` exposes one Fabric view, `SplatKitView`, over a Metal adapter on iOS and a Vulkan adapter on Android.
The renderer stays native; React Native passes a world, display settings and a single render policy across the boundary and receives stats, capability and world events back.

## Set up

Requires the Node version in `package.json` `engines` (22.13 or newer).

```sh
npm ci
```

## Check your change

```sh
npm run check
```

`npm run check` runs, in order:

- `npm run typecheck` - TypeScript against the package sources.
- `npm test` - compiles, then runs the contract tests in `tests/` with `node --test`.
- `npm run codegen` - Fabric Codegen against the real, pinned generators.

The [contract workflow](.github/workflows/contract.yml) runs the same command on every pull request, so a local pass is the expected baseline.

## Native adapter changes

Changes under `android/` or `ios/` need checks that cannot run in this repository, because the export ships the package but not the host tests or the example app.
Run them from the SplatKit monorepo:

- Android adapter host tests:

  ```sh
  apps/react-native/android/gradlew -p packages/react-native-splatkit/android/verification \
    :adapter:compileDebugKotlin :adapter:testDebugUnitTest
  ```

- The [React Native example](https://github.com/Xget7/splatkit/tree/main/apps/react-native) on a device or emulator.
- For build wiring - the podspec, Gradle, Codegen or `package.json` `files` - install the `npm pack` tarball into a fresh React Native app outside the monorepo.

The podspec fetches and checksum-verifies `SplatKitCore.xcframework` during `prepack`.
Set `SPLATKIT_IOS_XCFRAMEWORK_PATH` to a local `scripts/package-ios.sh` build while iterating on the iOS adapter.

## Report results honestly

State which checks you ran and which you skipped.
Name the device and driver for anything native or rendering, and say whether it was an emulator, a simulator or physical hardware.
An emulator is enough for logic; GPU behavior needs a real GPU.
Do not describe an approximation or a partial run as a full one.

## Documentation

Keep documentation short and put contracts beside the code they constrain.
Explain decisions in the pull request and in comments next to the code they shape.
Do not add planning or decision documents.

## Style

- Match the naming, structure and contracts already in the package.
- Plain dashes, never em dashes.
- One sentence per line in Markdown.

## License

By contributing you agree that your contributions are licensed under the [MIT License](LICENSE).
