# SplatKit agents

SDK integration or verification: read [the harness](docs/AGENT_HARNESS.md).
Implementation: follow [CONTRIBUTING.md](CONTRIBUTING.md); terminology lives in [CONTEXT.md](CONTEXT.md).
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
