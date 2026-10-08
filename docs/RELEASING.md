# Releasing

A version number is the release trigger.
Bump it in a pull request, merge to `main`, and the workflows publish; merge anything else and they do nothing.
Nobody tags by hand.

## The three artifacts

| Artifact | Version lives in | Published by | Registry |
| --- | --- | --- | --- |
| `io.github.xget7:splatkit-android` | `packages/splatkit-android/build.gradle.kts`, the `coordinates(...)` call | `.github/workflows/release.yml` | Maven Central |
| `@splatkit/react-native` | `packages/react-native-splatkit/package.json` | `publish.yml` in the React Native mirror | npm |
| splatkit-ios `SplatKitCore.xcframework` | `Package.swift`, the binary target URL | `ios-package.yml` builds; a person uploads, see below | GitHub Releases |

## What a merge to main does

1. `mirror.yml` regenerates both public repositories with `scripts/export-ios-source.py` and pushes them.
   The export refuses binaries, oversized files and anything matching a secret pattern, so a bad file fails the job instead of reaching a public repository.
2. `release.yml` compares the Maven coordinate against the previous commit.
   If it changed, it publishes to Maven Central, tags the commit and opens a GitHub release.
3. The React Native mirror's own `publish.yml` asks npm whether `package.json`'s version exists.
   If it does not, it publishes with provenance, tags and releases.

Both publish steps are no-ops when the version did not move, so an ordinary merge is safe.
The npm publisher serializes runs from current `main`, rejects registry errors, and repairs missing tags/releases against npm's recorded source commit on retry.

### The `latest` dist-tag

Prereleases publish under the `next` dist-tag.
While every published version is a prerelease, the workflow also points `latest` at the newest one, because otherwise plain `npm install @splatkit/react-native` hands out whatever was published first.
An older-version retry never moves `latest` backwards.
Once a stable version owns `latest`, the workflow stops touching it.

## Cutting an iOS release

Run [ios-package.yml](../.github/workflows/ios-package.yml) on the candidate commit, then download its named artifact into `build/ios-release`.
It contains the device/simulator archive, a SHA256 file and `build-provenance.json` naming the source commit and Xcode version.
Use those exact bytes and checksum for the release; rebuilding produces a different archive.
Publication still requires the [validation gates](VALIDATION.md) and a manual upload after the mirror receives the pins.

```sh
gh run download <run-id> -R Xget7/splatkit \
  --name splatkit-ios-<source-sha> --dir build/ios-release
```

A local Mac build remains available:

```sh
scripts/package-ios.sh                 # prints the artifact path and its checksum
```

Reuse `build/ios-distribution`; clear its SDK build caches only after an Xcode upgrade invalidates their SDK paths.

Then, in one pull request:

1. Set the binary target's `url` and `checksum` in `Package.swift` to the new tag and the printed checksum.
2. Set the same version and checksum in `packages/react-native-splatkit/scripts/ios-xcframework.json`, which the npm package fetches at `prepack`.
3. Update the version named in `README.md` and in both iOS READMEs.

Merge it and wait for `mirror.yml`, then create the release the URL now points at.
Swift Package Manager reads `Package.swift` at the tag, so tagging before the mirror lands the new checksum points the release at its own predecessor.

```sh
gh release create v0.1.0-beta.N -R Xget7/splatkit-ios \
  --title "SplatKit iOS 0.1.0 beta N" --notes "..." \
  build/ios-release/SplatKitCore.xcframework.zip
```

For a local build, substitute the exact archive path printed by `scripts/package-ios.sh`.

Not `--prerelease`, for the reason `release.yml` gives for the Android artifact: every pre-1.0 release is a prerelease, and marking them all prerelease leaves the releases page with no Latest at all.

Create the iOS release before any npm publish that pins it: `npm prepack` downloads the XCFramework and verifies the checksum, so a missing release fails the publish.

## Secrets

| Secret | Where | Used by |
| --- | --- | --- |
| `MIRROR_TOKEN` | this repository | `mirror.yml`, to push to both public repositories. A fine-grained token with Contents and Workflows write access to each; the built-in `GITHUB_TOKEN` cannot reach another repository. |
| `MAVEN_CENTRAL_USERNAME`, `MAVEN_CENTRAL_PASSWORD`, `SIGNING_KEY`, `SIGNING_KEY_PASSWORD` | this repository | `release.yml` |

npm needs no secret.
`@splatkit/react-native` names the mirror's `publish.yml` as its trusted publisher, with `npm dist-tag` allowed, so the job authenticates with its own OIDC token.
Moving the workflow or renaming the file means updating that setting on npmjs.com.

No workflow that runs on a pull request touches any of them, and none uses `pull_request_target`, so a fork's pull request can run the checks but cannot reach a credential.
