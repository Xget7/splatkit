# SplatKit agents

SDK integration or verification: read [the harness](docs/AGENT_HARNESS.md).
Implementation: follow [CONTRIBUTING.md](CONTRIBUTING.md); terminology lives in [CONTEXT.md](CONTEXT.md).
Keep documentation terse; link code/contracts instead of duplicating them.
Report skipped checks, approximation limits and emulator/physical-device provenance explicitly.

Build storage: reuse ignored build directories in this checkout across sessions and reference large fixtures by path.
Reserve session scratchpads for small disposable probes.
React Native iOS: use the [example scripts](apps/react-native/package.json); serialize builds sharing Derived Data.
Separate build directories are for concurrent builds or required clean-build verification.
