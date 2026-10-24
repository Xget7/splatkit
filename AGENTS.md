# SplatKit agents

SDK integration or verification: read [the harness](docs/AGENT_HARNESS.md).
Implementation: follow [CONTRIBUTING.md](CONTRIBUTING.md); terminology lives in [CONTEXT.md](CONTEXT.md).
Architecture (packages, load and frame pipeline on Metal and Vulkan): read [ARCHITECTURE.md](docs/ARCHITECTURE.md) before exploring; a change to a stage, GPU layout or package dependency updates it in the same PR.
Keep documentation terse; link code/contracts instead of duplicating them.
Report skipped checks, approximation limits and emulator/physical-device provenance explicitly.

Build storage: reuse ignored build directories in this checkout across sessions and reference large fixtures by path.
Reserve session scratchpads for small disposable probes.
React Native iOS: use the [example scripts](apps/react-native/package.json); serialize builds sharing Derived Data.
Separate build directories are for concurrent builds or required clean-build verification.

## Agent skills

### Issue tracker

GitHub issues on Xget7/splatkit, through `gh`.
See [docs/agents/issue-tracker.md](docs/agents/issue-tracker.md).

### Triage labels

The five default roles, each label named after its role.
See [docs/agents/triage-labels.md](docs/agents/triage-labels.md).

### Domain docs

Single-context, with [CONTEXT.md](CONTEXT.md) as the glossary.
See [docs/agents/domain.md](docs/agents/domain.md).

### Pull requests

Every pull request body follows the [template](.github/PULL_REQUEST_TEMPLATE.md): Summary, Evidence, Merge Danger.
Changes that may materially affect FPS, frame/GPU milliseconds or memory bandwidth follow [visual benchmark evidence](docs/VALIDATION.md#pr-benchmark-evidence).
When measurements show a substantial effect, embed a before/after chart in the PR's Evidence for the engineer reviewing it; raw logs or a table alone are insufficient.
Split dependent work into a GitHub stack of one PR per concern with `gh stack` (`gh extension install github/gh-stack`).
