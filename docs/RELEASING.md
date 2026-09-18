# Releasing

A version number is the release trigger.
Bump it in a pull request, merge to `main`, and the workflows publish; merge anything else and they do nothing.
Nobody tags by hand.

## The three artifacts

| Artifact | Version lives in | Published by | Registry |
| --- | --- | --- | --- |
| `io.github.xget7:splatkit-android` | `packages/splatkit-android/build.gradle.kts`, the `coordinates(...)` call | `.github/workflows/release.yml` | Maven Central |
| `@splatkit/react-native` | `packages/react-native-splatkit/package.json` | `publish.yml` in the React Native mirror | npm |
| splatkit-ios `SplatKitCore.xcframework` | `Package.swift`, the binary target URL | a person, see below | GitHub Releases |

## What a merge to main does

1. `mirror.yml` regenerates both public repositories with `scripts/export-ios-source.py` and pushes them.
   The export refuses binaries, oversized files and anything matching a secret pattern, so a bad file fails the job instead of reaching a public repository.
2. `release.yml` compares the Maven coordinate against the previous commit.
   If it changed, it publishes to Maven Central, tags the commit and opens a GitHub release.
3. The React Native mirror's own `publish.yml` asks npm whether `package.json`'s version exists.
   If it does not, it publishes with provenance, tags and releases.

Both publish steps are no-ops when the version did not move, so an ordinary merge is safe.

### The `latest` dist-tag

Prereleases publish under the `next` dist-tag.
While every published version is a prerelease, the workflow also points `latest` at the newest one, because otherwise plain `npm install @splatkit/react-native` hands out whatever was published first.
Once a stable version owns `latest`, the workflow stops touching it.

## Cutting an iOS release

The XCFramework is built on a Mac, so this part is still manual.

```sh
rm -rf build/ios-distribution          # a cached Xcode SDK path breaks configure after an upgrade
scripts/package-ios.sh                 # prints the artifact path and its checksum
```

Then, in one pull request:

1. Set the binary target's `url` and `checksum` in `Package.swift` to the new tag and the printed checksum.
2. Set the same version and checksum in `packages/react-native-splatkit/scripts/ios-xcframework.json`, which the npm package fetches at `prepack`.
3. Update the version named in `README.md` and in both iOS READMEs.

Merge it, then create the release the URL now points at:

```sh
gh release create v0.1.0-alphaN -R Xget7/splatkit-ios --prerelease \
  --title "SplatKit iOS 0.1.0 alpha N" --notes "..." \
  build/ios-distribution/package.*/SplatKitCore.xcframework.zip
```

Create the iOS release before any npm publish that pins it: `npm prepack` downloads the XCFramework and verifies the checksum, so a missing release fails the publish.

## Secrets

| Secret | Where | Used by |
| --- | --- | --- |
| `MIRROR_TOKEN` | this repository | `mirror.yml`, to push to both public repositories. A fine-grained token with Contents and Workflows write access to each; the built-in `GITHUB_TOKEN` cannot reach another repository. |
| `MAVEN_CENTRAL_USERNAME`, `MAVEN_CENTRAL_PASSWORD`, `SIGNING_KEY`, `SIGNING_KEY_PASSWORD` | this repository | `release.yml` |
| `NPM_TOKEN` | the React Native mirror | `publish.yml` |

No workflow that runs on a pull request touches any of them, and none uses `pull_request_target`, so a fork's pull request can run the checks but cannot reach a credential.
