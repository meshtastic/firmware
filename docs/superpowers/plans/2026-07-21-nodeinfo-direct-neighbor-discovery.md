# NodeInfo Direct-Neighbor Discovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Limit reset-discovery NodeInfo replies to direct radio neighbors and keep each reply at zero hops.

**Architecture:** Keep generic reply construction in `MeshModule`. Add a protected virtual response-hop hook whose default delegates to `RoutingModule`; override it in `NodeInfoModule` only for direct broadcast NodeInfo discovery. Its allocator rejects relayed and unknown-distance broadcast requests while retaining unicast behavior.

**Tech Stack:** C++17, Unity native tests, PlatformIO, trunk.

## Global Constraints

- Do not change unicast NodeInfo response behavior.
- A broadcast request qualifies only when `getHopsAway(request) == 0`.
- Qualifying reply packets must have `hop_limit = 0`.
- Suppressed broadcasts must not result in a routing NO_RESPONSE NAK.
- Do not edit generated protobuf sources.

---

### Task 1: Add a response-hop hook

**Files:**
- Modify: `src/mesh/MeshModule.h:226-239`
- Modify: `src/mesh/MeshModule.cpp:231-240`
- Test: `test/test_mesh_module/test_main.cpp`

**Interfaces:** Produces `virtual uint8_t MeshModule::getResponseHopLimit(const meshtastic_MeshPacket &req)`, which returns `routingModule->getHopLimitForResponse(req)` by default.

- [ ] **Step 1: Write the failing test** — Add a synthetic reply module overriding `getResponseHopLimit()` to return `0`; dispatch a request and assert its captured reply has `hop_limit == 0`.
- [ ] **Step 2: Run the failing test** — Run `~/.platformio/penv/bin/python -m platformio test -e native -f test_mesh_module`; expect a compilation failure because the hook does not exist.
- [ ] **Step 3: Write minimal implementation** — Declare the protected virtual hook in `MeshModule`; implement it using `routingModule->getHopLimitForResponse(req)`; after `setReplyTo(r, req)` in `sendResponse`, assign `r->hop_limit = getResponseHopLimit(req)`.
- [ ] **Step 4: Run the focused test** — Run `~/.platformio/penv/bin/python -m platformio test -e native -f test_mesh_module`; expect PASS.
- [ ] **Step 5: Commit** — `git add src/mesh/MeshModule.h src/mesh/MeshModule.cpp test/test_mesh_module/test_main.cpp && git commit -m "refactor: allow modules to constrain response hop limits"`.

### Task 2: Restrict NodeInfo broadcast discovery

**Files:**
- Modify: `src/modules/NodeInfoModule.h:42-56`
- Modify: `src/modules/NodeInfoModule.cpp:138-192`
- Test: `test/test_mesh_module/test_main.cpp`

**Interfaces:** Consumes Task 1's hook and `getHopsAway()` from `src/mesh/NodeDB.h`. Produces NodeInfo replies only for direct broadcast discovery with zero hops.

- [ ] **Step 1: Write failing tests** — Build direct (`hop_start == hop_limit`), relayed (`hop_limit < hop_start`), unknown-hop (zero hop start and no decoded bitfield), and unicast NodeInfo requests. Assert only direct broadcast qualifies and its reply hop limit is zero; unicast retains the routing default.
- [ ] **Step 2: Run the failing test** — Run `~/.platformio/penv/bin/python -m platformio test -e native -f test_mesh_module`; expect a compilation failure because NodeInfo lacks the policy hook.
- [ ] **Step 3: Write minimal implementation** — In `NodeInfoModule::allocReply`, detect an external broadcast request and return `NULL` with `ignoreRequest = true` unless `getHopsAway(*currentRequest) == 0`. Override the hook to return `0` only for a broadcast request at zero hops and otherwise call `MeshModule::getResponseHopLimit(req)`.
- [ ] **Step 4: Run focused tests** — Run `~/.platformio/penv/bin/python -m platformio test -e native -f test_mesh_module`; expect PASS for direct, relayed, unknown-hop, and unicast controls.
- [ ] **Step 5: Commit** — `git add src/modules/NodeInfoModule.h src/modules/NodeInfoModule.cpp test/test_mesh_module/test_main.cpp && git commit -m "fix: limit NodeInfo discovery to direct neighbors"`.

### Task 3: Format and validate

**Files:**
- Modify: only formatting reported by `trunk fmt`.

- [ ] **Step 1: Format** — Run `trunk fmt`; expect exit 0.
- [ ] **Step 2: Run focused native tests** — Run `~/.platformio/penv/bin/python -m platformio test -e native -f test_mesh_module`; expect PASS.
- [ ] **Step 3: Run repository wrapper** — Run `./bin/run-tests.sh`; expect GREEN, or record the exact host blocker without calling it a pass.
- [ ] **Step 4: Inspect and commit** — Run `git diff --check origin/develop...HEAD`, `git status --short`, and `git log --format='%h %an <%ae> %s' origin/develop..HEAD`; then commit validation-only formatting changes if present.
