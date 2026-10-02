---
name: write-native-tests
description: Write or change a native C++ unit test under test/ - naming, the header comment, making code testable, and proving each test fails on broken code as well as passing on correct code. Use whenever adding or editing a test_* function or suite.
---

# Writing native tests

The rules are in [`.github/copilot-instructions.md`](../../../.github/copilot-instructions.md): [test naming](../../../.github/copilot-instructions.md#test-naming), [test comments](../../../.github/copilot-instructions.md#test-comments), and [`test/README.md`](../../../test/README.md) for suite skeletons, shims and mocks. This skill adds the procedure and the traps. Run tests with the `run-native-tests` skill.

## 1. Make the behaviour reachable

- Most suites never build `RadioLibInterface` or a driver. Factor the **decision** into a pure function (`constexpr` where possible) in a header the suite already includes, and test that. Example: `isLr20x0BandHop()` in `src/mesh/LR20x0Band.h`, pinned in `test_radio`. Don't create a new file for a ten-line helper.
- To reach protected members, use a test shim subclass (`test/README.md`, "Test Shim"), not `#define private public`.
- Register every case with `RUN_TEST(...)` in `setup()`, and end with `exit(UNITY_END())` on every branch. An unregistered test never runs and never fails.

## 2. Name it

- A `test_` prefix, then `_`-separated segments. Case inside a segment is free. The suite directory is strictly `test_[a-z0-9_]+`.
- `.trunk/trunk.yaml` exempts every `test/**/test_main.cpp` from trufflehog, whose Lob detector would otherwise flag any `test_` name with exactly 35 characters after the prefix. That shape still trips it in any other file, such as a second source file in a suite.

## 3. Write the header comment

At whatever length it needs: **what** is under test (symbol and file), **why** that behaviour is required, and **which regression** returns if the assertions are deleted or relaxed. Per-case comments stay short and appear only where an assertion turns on something non-obvious. The 1-2 line limit for `src/` does not apply here.

## 4. Prove it both ways

A test that has only ever passed is unproven: it may be unable to fail. Every new or changed test must be seen to do both:

1. **Pass on the correct code.** Run the suite and read `RESULT: FILTERED`/`GREEN` with the case listed as passed.
2. **Fail on demand.** Break the code under test in the exact way the test claims to guard against, run again, and read `RESULT: RED` with **this test** named as the failure. Useful mutations:
   - flip a boundary (`<` ↔ `<=`, `>=` ↔ `>`); a boundary test needs one case on each side
   - invert or drop a condition, or return the other enum value
   - delete the call, or the state reset, that the behaviour depends on
   - for "never X" tests, make the code do X once
3. **Restore, and run green again.** Confirm the tree matches what you meant to commit (`git diff`).

If no mutation you can think of turns the test red, the test asserts nothing. Rewrite it; don't ship it. Say in the PR or commit message which mutation each test kills. For example: "`>=`→`>` at the window boundary fails `test_…_windowEndsAtExactlyOneMaxPacket`".

Do not leave a mutation in the tree. Revert the source file itself, and check `git status` before committing.

## 5. Shared state

Each suite runs in its own sandbox `$HOME`. A suite that writes persisted state on purpose declares it in `test/state-manifest.tsv` with a reason. `./bin/run-tests.sh --write-manifest` prints the entries a run would need; a human pastes and justifies them.
