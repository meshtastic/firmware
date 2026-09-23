---
name: write-native-tests
description: Write or change a native C++ unit test under test/ - naming, the header comment, the trufflehog/Lob name trap, making code testable, and proving each test fails on broken code as well as passing on correct code. Use whenever adding or editing a test_* function or suite.
---

# Writing native tests

The rules are in [`.github/copilot-instructions.md`](../../../.github/copilot-instructions.md): [test naming](../../../.github/copilot-instructions.md#test-naming), [test comments](../../../.github/copilot-instructions.md#test-comments), and [`test/README.md`](../../../test/README.md) for suite skeletons, shims and mocks. This skill adds the procedure and the traps. Run tests with the `run-native-tests` skill.

## 1. Make the behaviour reachable

- Most suites never build `RadioLibInterface` or a driver. Factor the **decision** into a pure function (`constexpr` where possible) in a header the suite already includes, and test that. Example: `staleRxFlagAction()` in `src/mesh/RadioInterface.h`, pinned in `test_radio`. Don't create a new file for a ten-line helper.
- To reach protected members, use a test shim subclass (`test/README.md`, "Test Shim"), not `#define private public`.
- Register every case with `RUN_TEST(...)` in `setup()`, and end with `exit(UNITY_END())` on every branch. An unregistered test never runs and never fails.

## 2. Name it

- A `test_` prefix, then `_`-separated segments. Case inside a segment is free. The suite directory is strictly `test_[a-z0-9_]+`.
- **The trufflehog/Lob trap.** trufflehog's Lob detector matches `\b(test|live)_[A-Za-z0-9_]{35}\b`. A test name with **exactly 35 characters after `test_`** (underscores count) is flagged as a leaked API key, on both the definition and the `RUN_TEST` line, and CI's Trunk Check fails the PR. `trunk fmt` does not run trufflehog. Check before pushing:

  ```bash
  grep -o 'test_[A-Za-z0-9_]*' test/<suite>/test_main.cpp | sort -u | awk 'length($0)-5==35'
  ```

  It must print nothing. Rename (34 or 36 characters is fine). Never allow-list a name. Only new hits block, so the 35-character names already in the tree are no licence.

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
