# Chunked PhoneAPI regression tests

The native suite exercises ordered client API transport independently of the
legacy mesh `ChunkedPayload` protocol. It compares negotiated config replies with
ordinary replies and compares the small-frame encoder with nanopb at the size
boundaries. A test-only encoder adapter exists solely to supply exact byte totals.

Only BLE needs chunking and the firmware allows one BLE connection, so there is a
single file-scope slot. A `TYPE_BLE` client allocates it on its first valid chunk
start; any other client's chunked request is ignored and never allocates, and its
replies are never chunked. `close()` (and so the destructor) frees the slot. The
feature compiles out on builds without Bluetooth or with
`-DMESHTASTIC_EXCLUDE_CHUNKED_PHONEAPI=1` (see `HAS_CHUNKED_PHONEAPI` in
`configuration.h`).

Slot ownership lasts until `close()` or destruction, including before the config
handshake establishes `isConnected()`. A completed input pins the slot while the
normal virtual `handleToRadio()` path runs. Same-client nested chunks are rejected;
other clients' plain requests still dispatch. Closing during dispatch clears
buffers and opt-in immediately but defers freeing the slot until that dispatch
returns. Tests cover both re-entrant and cross-thread close/dispatch interleavings.

The BLE receive callbacks share a tested duplicate predicate: repeated chunk
envelopes are not suppressed, while legacy duplicate writes retain their existing
behaviour. Recognition scans protobuf tags and honours unknown fields and the
last oneof, rather than assuming the first byte is `0x42`. A plain config request
resets chunk negotiation and pending fragments; an internally reassembled config
request preserves negotiation. Pending fragments also count towards `available()`.
As elsewhere in PhoneAPI, transports must serialise ordinary same-client requests
and must not destroy an object while a method is active.

## Dependent protobuf PR

Firmware PR10928 depends on protobuf PR980 (`ClientApiChunkedPayload`, ToRadio
field 8 and FromRadio field 20). The submodule points to that change rebased on
the protobufs revision pinned by firmware develop, until PR980 merges.

The generated bindings in `src/mesh/generated/` carry only the new message and
oneof members, so the code builds before PR980 merges. They must be replaced by
the upstream protobuf workflow's regenerated bindings once PR980 merges.
