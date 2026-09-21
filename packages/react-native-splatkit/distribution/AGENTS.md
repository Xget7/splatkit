# SplatKit React Native agents

This repository is the generated distribution of `packages/react-native-splatkit` from the [SplatKit](https://github.com/Xget7/splatkit) monorepo.
Do not make real changes here: the mirror overwrites this tree on every merge, so edit the package upstream and let the mirror regenerate it.
Read [CONTRIBUTING.md](CONTRIBUTING.md) before editing, and keep GPU internals native.

## Integration

- [README](README.md) - public API, props, events and commands.
- [Android adapter](android/README.md) - Kotlin adapter, host tests and lifecycle rules.

## Layout

- `src/` - the JavaScript surface: `contracts.ts`, `performance.ts`, `index.ts` and the Codegen spec in `src/specs/SplatViewNativeComponent.ts`.
- `tests/` - Node contract tests for the JavaScript surface and both adapters.
- `android/` - Kotlin Fabric adapter over the Android SDK; `android/verification/` holds its host tests.
- `ios/` - Objective-C++ Fabric adapter over `SKSplatEngine`; `SplatKitCore.xcframework` is fetched at `prepack`, not committed.
- `scripts/` - Codegen and XCFramework fetch, `SplatKitReactNative.podspec`, `react-native.config.js`.

## Verify

```sh
npm ci
npm run check
```

`npm run check` is `typecheck`, the `tests/` suite and Fabric Codegen, and the [contract workflow](.github/workflows/contract.yml) runs it on every pull request.
Native adapter changes are verified upstream in SplatKit, where the adapter host tests and the React Native example live:

```sh
apps/react-native/android/gradlew -p packages/react-native-splatkit/android/verification \
  :adapter:compileDebugKotlin :adapter:testDebugUnitTest
```

## Invariants

- One policy contract, parsed identically in JavaScript, Kotlin, Objective-C++ and C++.
- Keep rendering decisions in native code; do not move GPU work or policy evaluation into JavaScript.
- The Codegen spec is the source of truth for props, events and commands; change it and regenerate rather than editing generated output.
- Props commit once per Fabric transaction; a world change with settings or a policy configures the new engine exactly once.
- Contracts belong beside the code they constrain, in short docstrings or the relevant README.

## Evidence

Report skipped checks, approximation limits and emulator versus physical-device provenance explicitly.
A simulator or emulator run does not establish GPU behavior.
Never present a partial or approximated result as a complete one.
