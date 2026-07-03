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
> |                                             |                                                            |
> | ------------------------------------------- | ---------------------------------------------------------- |
> | General helpers (clamp, UTF-8, string fmt…) | `src/meshUtils.h`                                          |
> | Logging macros (LOG_DEBUG / INFO / WARN…)   | `src/DebugConfiguration.h`                                 |
> | New module skeleton                         | inherit `ProtobufModule<T>` in `src/mesh/ProtobufModule.h` |
> | Observer / event wiring                     | `src/Observer.h`                                           |

**Read `.github/copilot-instructions.md` first.** That file is the canonical agent-facing document for this repo. It covers project layout, coding conventions, the build system, CI/CD, the native C++ test suite, and the MCP Server & Hardware Test Harness. Read it top-to-bottom before starting any non-trivial change.

This file (`CLAUDE.md`) is a short pointer for Claude Code sessions. Slash commands live in `.claude/commands/`.
