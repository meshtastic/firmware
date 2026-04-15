#pragma once

#include <vector>

#include "NodeStore.h"

// Thin in-memory NodeStore that keeps the old vector semantics alive while
// NodeDB moves to slot-oriented logic. On PSRAM builds it also clamps the
// requested slot count to what the device can realistically hold right now.
class ArraySlotStore : public NodeStore
{
  public:
    explicit ArraySlotStore(std::vector<meshtastic_NodeInfoLite> &slots);

    void reset(size_t slotCount) override;
    void resize(size_t slotCount) override;
    size_t slotCount() const override;

    meshtastic_NodeInfoLite &slot(size_t index) override;
    const meshtastic_NodeInfoLite &slot(size_t index) const override;

    meshtastic_NodeInfoLite *data() override;
    const meshtastic_NodeInfoLite *data() const override;

    void clearSlots(size_t beginIndex, size_t endIndex) override;

  private:
    // Runtime cap selection stays in the backend so NodeDB can ask for the
    // build-time target and let the store decide what the platform can afford.
    size_t constrainSlotCount(size_t requestedSlotCount) const;

    std::vector<meshtastic_NodeInfoLite> &slots;
};
