#include "ArraySlotStore.h"

#include <algorithm>
#include <assert.h>

ArraySlotStore::ArraySlotStore(std::vector<meshtastic_NodeInfoLite> &slots) : slots(slots) {}

void ArraySlotStore::reset(size_t slotCount)
{
    slots = std::vector<meshtastic_NodeInfoLite>(slotCount);
}

void ArraySlotStore::resize(size_t slotCount)
{
    slots.resize(slotCount);
}

size_t ArraySlotStore::slotCount() const
{
    return slots.size();
}

meshtastic_NodeInfoLite &ArraySlotStore::slot(size_t index)
{
    return slots.at(index);
}

const meshtastic_NodeInfoLite &ArraySlotStore::slot(size_t index) const
{
    return slots.at(index);
}

meshtastic_NodeInfoLite *ArraySlotStore::data()
{
    return slots.data();
}

const meshtastic_NodeInfoLite *ArraySlotStore::data() const
{
    return slots.data();
}

void ArraySlotStore::clearSlots(size_t beginIndex, size_t endIndex)
{
    if (beginIndex >= endIndex) {
        return;
    }

    assert(endIndex <= slots.size());
    std::fill(slots.begin() + beginIndex, slots.begin() + endIndex, meshtastic_NodeInfoLite());
}
