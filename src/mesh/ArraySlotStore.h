#pragma once

#include <vector>

#include "NodeStore.h"

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
    size_t constrainSlotCount(size_t requestedSlotCount) const;

    std::vector<meshtastic_NodeInfoLite> &slots;
};
