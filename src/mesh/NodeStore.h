#pragma once

#include <stddef.h>

#include "mesh/generated/meshtastic/deviceonly.pb.h"

// Slot-oriented view of the full NodeInfoLite backing store.
// NodeDB keeps dense storage indices in its own domain model and uses this
// seam so RAM-backed and flash-backed implementations can share the same code.
class NodeStore
{
  public:
    virtual ~NodeStore() = default;

    virtual void reset(size_t slotCount) = 0;
    virtual void resize(size_t slotCount) = 0;
    virtual size_t slotCount() const = 0;

    virtual meshtastic_NodeInfoLite &slot(size_t index) = 0;
    virtual const meshtastic_NodeInfoLite &slot(size_t index) const = 0;

    // Exposed for the remaining pointer-based compatibility paths and for
    // recovering a dense storage index from a NodeInfoLite*.
    virtual meshtastic_NodeInfoLite *data() = 0;
    virtual const meshtastic_NodeInfoLite *data() const = 0;

    virtual void clearSlots(size_t beginIndex, size_t endIndex) = 0;
};
