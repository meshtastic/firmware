# Claude Code instructions

> **TL;DR**
>
> |                |                                                                                                                        |
> | -------------- | ---------------------------------------------------------------------------------------------------------------------- |
> | Local tests    | `./bin/run-tests.sh` (exit 0 GREEN · 1 RED · 2 AMBER · 3 FILTERED)                                                     |
> | Hardware tests | [meshtastic/meshtastic-mcp](https://github.com/meshtastic/meshtastic-mcp) (`MESHTASTIC_FIRMWARE_ROOT` → this checkout) |
> | Format         | `trunk fmt`                                                                                                            |
> | Mirror docs    | `.github/copilot-instructions.md` (canonical) · `AGENTS.md`                                                            |
>
> **Need this? It's here.**
>
> |                                                           |                                                            |
> | --------------------------------------------------------- | ---------------------------------------------------------- |
> | General helpers (clamp, UTF-8, string fmt…)               | `src/meshUtils.h`                                          |
> | Logging macros (LOG_DEBUG / INFO / WARN…)                 | `src/DebugConfiguration.h`                                 |
> | Elapsed time / deadlines (never bare `millis()` compares) | `src/mesh/Throttle.h`                                      |
> | New module skeleton                                       | inherit `ProtobufModule<T>` in `src/mesh/ProtobufModule.h` |
> | Observer / event wiring                                   | `src/Observer.h`                                           |

**Read `.github/copilot-instructions.md` first.** That file is the canonical agent-facing document for this repo. It covers project layout, coding conventions, the build system, CI/CD, the native C++ test suite, and the MCP Server & Hardware Test Harness. Read it top-to-bottom before starting any non-trivial change.

This file (`CLAUDE.md`) is a short pointer for Claude Code sessions. Slash commands live in `.claude/commands/`.

## House rule: documentation does not live in this repo

This repository holds firmware code. There is no `docs/` directory - the design documents that used to sit there were published to [meshtastic/meshtastic](https://github.com/meshtastic/meshtastic) in #11488 and the directory was deleted - and it must not come back. Do not create a `.md` file to describe a feature, a configuration surface, an API, a wire format, or a design; write it in the docs repo and link that PR instead. Never leave a write-up behind in the tree: no investigation notes, no mitigation plans, no migration checklists, no "how we got here" narrative, no summaries of what a change did. That is what the PR description and the commit message are for, and they are the only place it belongs. When you do write documentation upstream, write a technical manual, not a novel - what the feature does, the settings it exposes in the user's terms, and the exact API or protocol a client speaks. No story of the debugging journey, no rationale essays, no changelog prose. Concise and factual, as short as the facts allow.
