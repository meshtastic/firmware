# Claude Code slash commands for the mcp-server test suite

Three AI-assisted workflows wrapping `mcp-server/run-tests.sh` and the meshtastic MCP tools. Each one has a twin in `.github/prompts/` for Copilot users.

| Slash command         | What it does                                                              | Copilot equivalent                       |
| --------------------- | ------------------------------------------------------------------------- | ---------------------------------------- |
| `/test [args]`        | Runs the test suite (auto-detects hardware) and interprets failures       | `.github/prompts/mcp-test.prompt.md`     |
| `/diagnose [role]`    | Read-only device health report via the meshtastic MCP tools               | `.github/prompts/mcp-diagnose.prompt.md` |
| `/repro <test> [n=5]` | Re-runs one test N times, diffs firmware logs between passes and failures | `.github/prompts/mcp-repro.prompt.md`    |

## Why two surfaces

The Claude Code commands and Copilot prompts cover the same three workflows but each speaks its host's idiom:

- **Claude Code** (`/test`) uses `$ARGUMENTS` for pass-through, has direct access to Bash + all MCP tools registered in the user's settings, and runs in the terminal context.
- **Copilot** (`/mcp-test`) runs in VS Code's agent mode; it has terminal + MCP access too but typically asks the operator to confirm inputs interactively.

A contributor using either IDE gets equivalent assistance. Keep the two in sync when behavior changes — the diff of intent should be minimal.

## House rules

- **No destructive writes without explicit operator approval.** Skills that could reflash, factory-reset, or reboot a device must describe the action and stop — the operator authorizes.
- **Interpret failures, don't just echo them.** The skill body should pull firmware log lines from `mcp-server/tests/report.html` (the `Meshtastic debug` section, attached by `tests/conftest.py::pytest_runtest_makereport`) and classify the failure.
- **Keep MCP tool calls sequential per port.** SerialInterface holds an exclusive port lock; two parallel tool calls on the same port deadlock.
- **Never speculate about root cause.** If the evidence doesn't support a classification, say "unknown" and list what you'd need to disambiguate.

## Adding a new command

1. Write the Claude Code version at `.claude/commands/<name>.md` with YAML frontmatter:

   ```yaml
   ---
   description: one-line purpose (used for auto-invocation by the model)
   argument-hint: [optional-hint]
   ---
   ```

2. Write the Copilot equivalent at `.github/prompts/mcp-<name>.prompt.md` with:

   ```yaml
   ---
   mode: agent
   description: ...
   ---
   ```

3. Add the row to the table above. Cross-link in both bodies.

4. Smoke-test on Claude Code first (`/<name>` should appear in autocomplete), then in VS Code Copilot (`/mcp-<name>` in Chat).
