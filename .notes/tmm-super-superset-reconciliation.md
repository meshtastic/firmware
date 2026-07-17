# tmm-super-superset: reconciliation of tmm-fix-2 and tmm-fix-superset

Two branches implemented the same design brief independently on top of the shared
`tmm-fix` series: keep the TrafficManagement NodeInfo cache a superset of NodeDB
(hot store ∪ warm tier), with tick-based clocks, write-through hooks, and
purge-on-removal. This branch combines them, taking each divergence on its merits.
Base: `origin/develop` (8ebdae9) + the rebased `tmm-fix` series, plus cherry-picks
of upstream #11035 (dependency), PR #11048 (2 commits), PR #11046, PR #11047 (3).

## Decision matrix

| # | Divergence | tmm-fix-2 | tmm-fix-superset | Taken | Why |
|---|-----------|-----------|------------------|-------|-----|
| 1 | Native test coverage | `TMM_HAS_NODEINFO_CACHE` macro: cache builds on heap under ARCH_PORTDUINO+PIO_UNIT_TESTING, so all cache tests run in CI; `dropNodeInfoCacheForTest()` for fallback-path tests | none - cache tests compile only for ESP32+PSRAM (hardware-only) | **tmm-fix-2** | CI coverage of the security-relevant paths was a review finding; hardware-only tests rot. |
| 2 | Warm-tier key pin | `NodeDB::copyPublicKeyAuthoritative()` (hot→warm), reused by `copyPublicKey()`; TMM pins against it | inline `nodeDB->warmStore.copyKey()` second check inside TMM | **tmm-fix-2** | Encapsulation: one authoritative-tier lookup owned by NodeDB; TMM doesn't reach into NodeDB internals; `copyPublicKey` reuses it so the tier order can never desync. |
| 3 | Retention | third tick clock (`retTick`, 1 h) + timed eviction: 7 d keyed / 6 h keyless for non-members; members re-stamped (keep-alive) | **no timed eviction at all**: slots reclaimed only by tiered LRU on insert or explicit purge; LRU age = `obsTick` with never-observed scored 0xFF (oldest) | **tmm-fix-superset** | Matches the design owner's explicit dislike of the 7-day eviction. Wrap-safety needs only obs/resp saturation (retention was the only reason `retTick` existed). A quiet entry keeps value (key pool, name rehydration). Drops a byte per entry. |
| 4 | Membership + seeding cadence | full two-direction reconcile every 60 s sweep (bitmap-marked clear pass), `spareMembers` eviction guard | per-entry membership refresh every sweep (entry→NodeDB `contains` checks); hot-store-only seeding, boot + hourly | **mixed** | Superset's cadence (cheap member-bit refresh each sweep; heavy seeding hourly + boot, write-through hooks give immediacy) is better balanced. But its seeding covers the hot store only - this branch keeps tmm-fix-2's warm-tier key-only seeding leg (via `WarmNodeStore::entryAt`) so the invariant is genuinely TMM ⊇ hot ∪ warm, and keeps the `spareMembers` guard because with warm seeding the member population can exceed the cache. |
| 5 | updateUser write-through | `signerKnown = nodeInfoLiteHasXeddsaSigned(info)`; provenance-carry via explicit `provenBefore` | `signerKnown = isVerifiedSignerForKey(nodeId, p.key)` (key-matched); simpler keyChanged/reset logic; self-skip at call site | **tmm-fix-superset** | Key-matched signer transfer is the stricter, self-documenting form; the simpler provenance logic is behaviorally equivalent (keyless commits leave the flag untouched, key changes reset it). Self-skip kept in both places (cheap, defensive). |
| 6 | Key-only commit hook | absent | `onNodeKeyCommitted(node, key32, proven)` + call sites in Router (admin-key learn) and KeyVerificationModule (manual verification, proven=true) | **tmm-fix-superset** | Genuine coverage gap in tmm-fix-2: both sites write `node->public_key` directly, bypassing `updateUser`. Manual verification is the strongest provenance the cache can carry. |
| 7 | Reset purging | `purgeNode()` from `removeNodeByNum()` only | also `purgeAll()` from `factoryReset()` and `resetNodes()` | **tmm-fix-superset** | Removal-is-full-removal applies to bulk resets too; don't rely on the post-reset reboot. |
| 8 | purgeNode implementation | open-coded scans of both arrays | reuses `findEntry`/`findNodeInfoEntryMutable`, logs the purge | **tmm-fix-superset** | Less duplication; the log line is genuinely useful for a user-initiated action. |
| 9 | Tick constants home | file-scope constexprs + free functions in the .cpp | header, next to the existing UnifiedCache tick helpers (`currentObsTick()` style) | **tmm-fix-superset** | Consistency with the established tick idiom in the same class. |
| 10 | `lastObservedRxTime` | dropped (was only echoed in one debug log) | kept for the debug log | **tmm-fix-2** | 4 B × 2000 = 8 KB of PSRAM for a log field; the tick-age log line carries the same signal. |
| 11 | Test hooks | `dropNodeInfoCacheForTest()` | `peekNodeInfoFlagsForTest()` (flag introspection) | **both** | Orthogonal: one enables fallback-path tests in native builds, the other lets saturation/membership tests assert sweep effects directly. |
| 12 | Fallback/serve/throttle gates | identical semantics | identical semantics (cosmetic differences) | tmm-fix-2 text | Same behavior; kept the wording already on this branch, with superset's better "stale vs never observed" log distinction folded in. |

## Entry layout after reconciliation

`node(4) + meshtastic_User(116) + obsTick + respTick + sourceChannel +
decodedBitfield + flags(1)` = 125 → 128 B padded (2000 entries = 256 KB PSRAM,
8 KB below the original develop footprint). Flag byte: hasDecodedBitfield,
keySignerProven, hasFullUser, hasObserved, hasResponded, isMember (2 spare bits).

## Tests

Union of both suites: tmm-fix-2's warm-pin, hot/warm seeding, updateUser hook,
and removal-purge tests (adapted: the TTL-based retention test is superseded by
no-TTL semantics) + tmm-fix-superset's key-hook provenance, tick-saturation, and
sweep-membership tests (adapted to run natively under TMM_HAS_NODEINFO_CACHE).
