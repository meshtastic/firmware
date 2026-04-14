#include "ArraySlotStore.h"

#include <algorithm>
#include <assert.h>

#include "configuration.h"
#include "memGet.h"
#include "mesh-pb-constants.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3) && defined(BOARD_HAS_PSRAM)
namespace
{

size_t slotsFromBudget(uint32_t availableBytes, size_t reservedBytes, size_t bytesPerSlot)
{
    if (bytesPerSlot == 0 || availableBytes <= reservedBytes) {
        return 0;
    }

    return (availableBytes - reservedBytes) / bytesPerSlot;
}

} // namespace
#endif

ArraySlotStore::ArraySlotStore(std::vector<meshtastic_NodeInfoLite> &slots) : slots(slots) {}

void ArraySlotStore::reset(size_t slotCount)
{
    slots = std::vector<meshtastic_NodeInfoLite>(constrainSlotCount(slotCount));
}

void ArraySlotStore::resize(size_t slotCount)
{
    const size_t actualSlotCount = constrainSlotCount(slotCount);

    slots.resize(actualSlotCount);
    if (slots.capacity() > actualSlotCount) {
        slots.shrink_to_fit();
    }
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

size_t ArraySlotStore::constrainSlotCount(size_t requestedSlotCount) const
{
#if defined(CONFIG_IDF_TARGET_ESP32S3) && defined(BOARD_HAS_PSRAM)
    const uint32_t freeHeap = memGet.getFreeHeap();
    const uint32_t freePsram = memGet.getFreePsram();
    const uint32_t totalPsram = memGet.getPsramSize();
    const size_t heapLimitedSlots =
        std::max<size_t>(1, slotsFromBudget(freeHeap, NODEDB_HEAP_HEADROOM_BYTES, NODEDB_ESTIMATED_DRAM_BYTES_PER_NODE));

    size_t actualSlotCount = requestedSlotCount;
    if (totalPsram == 0) {
        actualSlotCount = std::min(requestedSlotCount, static_cast<size_t>(get_default_esp32s3_max_num_nodes()));
    } else {
        const size_t psramLimitedSlots =
            std::max<size_t>(1, slotsFromBudget(freePsram, NODEDB_PSRAM_HEADROOM_BYTES, sizeof(meshtastic_NodeInfoLite)));
        actualSlotCount = std::min(requestedSlotCount, std::min(heapLimitedSlots, psramLimitedSlots));
    }

    if (actualSlotCount != requestedSlotCount) {
        LOG_WARN("ArraySlotStore cap reduced from %u to %u (freeHeap=%u freePsram=%u totalPsram=%u)",
                 static_cast<unsigned>(requestedSlotCount), static_cast<unsigned>(actualSlotCount), freeHeap, freePsram,
                 totalPsram);
    } else {
        // Large allocations on ESP32-S3 boards land in PSRAM after extmem is enabled at boot.
        LOG_INFO("ArraySlotStore cap %u slots (freeHeap=%u freePsram=%u totalPsram=%u)", static_cast<unsigned>(actualSlotCount),
                 freeHeap, freePsram, totalPsram);
    }

    return actualSlotCount;
#else
    return requestedSlotCount;
#endif
}
