# SplatKit agents

SDK integration or verification: read [the harness](docs/AGENT_HARNESS.md).
Implementation: follow [CONTRIBUTING.md](CONTRIBUTING.md); terminology lives in [CONTEXT.md](CONTEXT.md).
Architecture (packages, load and frame pipeline on Metal and Vulkan): read [ARCHITECTURE.md](docs/ARCHITECTURE.md) before exploring; a change to a stage, GPU layout or package dependency updates it in the same PR.
Keep documentation terse; link code/contracts instead of duplicating them.
Report skipped checks, approximation limits and emulator/physical-device provenance explicitly.
Use the [12M reference world](docs/VALIDATION.md#reference-performance-world) for renderer acceptance, performance and battery benchmarks; small worlds do not establish representative results.

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
Never add generated-by footers, AI/tool signatures, badges or promotional attribution to PR descriptions or commit messages, including "Generated with Claude Code".
Metal and Vulkan benchmarks share the [GPU profiling protocol](docs/GPU_PROFILING.md) and parameter preset; app-published timings do not establish GPU pass/frame analysis.
Changes that may materially affect FPS, frame/GPU milliseconds or memory bandwidth follow [visual benchmark evidence](docs/VALIDATION.md#pr-benchmark-evidence).
When measurements show a substantial effect, put native Markdown before/after tables in the PR's Evidence, with units, absolute/percentage changes and repeat ranges; do not embed benchmark images.
Do not accept an FPS optimization without demonstrated end-to-end benefit or with unresolved critical-stage regressions; validate thermal/workload comparability before attributing changes.
Split dependent work into a GitHub stack of one PR per concern with `gh stack` (`gh extension install github/gh-stack`).
