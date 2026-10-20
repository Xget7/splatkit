# Domain docs

How the engineering skills read this repo's domain language.

## Before exploring, read

- [CONTEXT.md](../../CONTEXT.md) at the repo root: the glossary of the code, the docs and the conversation.
  Where a skill says `GLOSSARY.md`, read and edit `CONTEXT.md` instead; do not create a `GLOSSARY.md`.

## Decisions

This repo keeps no ADRs or other decision documents, as [CONTRIBUTING.md](../../CONTRIBUTING.md) requires.
Decisions live in pull request descriptions and in comments beside the code they shape.
Where a skill says to read or write `docs/adr/`, read the relevant PRs and code comments, and put a new decision in the PR description.

## Use the glossary's vocabulary

When output names a domain concept (an issue title, a refactor proposal, a test name, a PR body), use the term as `CONTEXT.md` defines it, and avoid the synonyms it lists under _Avoid_.
A concept missing from the glossary is either language the project does not use or a real gap; a gap goes into `CONTEXT.md` in the same PR.
