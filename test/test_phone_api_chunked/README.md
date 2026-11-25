# Chunked PhoneAPI regression tests

The native suite exercises ordered client API transport independently of the
legacy mesh `ChunkedPayload` protocol. It compares negotiated config replies with
ordinary replies and compares the small-frame encoder with nanopb at the size
boundaries. A test-only encoder adapter exists solely to supply exact byte totals.

Slot ownership lasts until `close()` or destruction, including before the config
handshake establishes `isConnected()`. A completed input pins its slot while the
normal virtual `handleToRadio()` path runs. Same-client nested chunks are rejected;
another client can still dispatch. Closing during dispatch clears buffers and
opt-in immediately but defers slot reuse until that dispatch returns. Tests cover
both re-entrant and deterministic cross-thread close/dispatch interleavings.

The BLE receive callbacks share a tested duplicate predicate: repeated chunk
envelopes are not suppressed, while legacy duplicate writes retain their existing
behaviour. Recognition scans protobuf tags and honours unknown fields and the
last oneof, rather than assuming the first byte is `0x42`. A plain config request
resets chunk negotiation and pending fragments for serial-client reuse; an
internally reassembled config request preserves negotiation. Pending fragments
also count towards `available()`, including the WASM polling API.

The overlap test holds two clients' dispatches simultaneously on separate threads,
checks capacity and same-client rejection, then releases one while the other
remains pinned. As elsewhere in PhoneAPI, transports must serialise ordinary
same-client requests and must not destroy an object while a method is active.

## Dependent protobuf PR

Firmware PR10928 depends on protobuf PR980 (`ClientApiChunkedPayload`, ToRadio
field 8 and FromRadio field 20). The refresh merges the protobuf revision pinned
by firmware develop, rather than unrelated later master schema changes. The
submodule points to the local dependency merge until publication is authorised.

The repository prohibits newly editing or committing `src/mesh/generated/`
(`AGENTS.md`, house rules, and `.github/copilot-instructions.md`, protobuf messages).
This refresh inherits the existing PR bindings and develop bindings through a
normal merge, without hand-editing or replacing generated files. The upstream
protobuf workflow must supply the canonical regenerated bindings before the
dependency can be considered integrated. Local nanopb reproduction is a separate
verification requirement, not a reason to override that rule.
