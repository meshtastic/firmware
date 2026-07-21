# NodeInfo direct-neighbor discovery

## Problem

A reset or radio-generation change sends a broadcast NodeInfo with
`want_response`. In a large mesh every receiver can answer, and normal reply
routing gives a direct receiver a two-hop reply. That creates an avoidable
fan-out burst while the existing 12-hour suppression window can also leave a
fresh node with no immediate peer identity.

## Design

Keep the discovery announcement as a normal broadcast so the new node's
identity remains visible throughout the mesh. Restrict the response:

- Only a broadcast `NODEINFO_APP` request received at exactly zero hops may
  produce a NodeInfo response.
- That response is unicast to the requester with `hop_limit = 0`; it cannot be
  relayed.
- Relayed broadcasts and packets whose hop distance cannot be established do
  not reply and suppress the automatic no-response NAK.
- Unicast NodeInfo exchanges, phone/API-triggered requests, and traffic
  management direct-response behavior remain unchanged.

The hop predicate uses `getHopsAway()`, which treats a decoded packet with
`hop_start == hop_limit` as a direct radio reception and rejects legacy or
malformed packets when the distance is unknown.

## Implementation

Add NodeInfo-specific helpers for qualifying a broadcast discovery request and
for choosing its reply hop limit. The generic `MeshModule` response path calls
the module hook after `setReplyTo()`, so only NodeInfo can replace the normal
return-path calculation with zero hops.

## Verification

Add native tests for direct, relayed, and unknown-hop broadcast NodeInfo
requests, plus a unicast control. Verify that only the direct broadcast reply
has a zero hop limit. Run formatting and the targeted native test suite; run
the full native wrapper when its host dependencies permit it.
