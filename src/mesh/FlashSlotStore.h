#pragma once

#include <stdint.h>

#include "mesh/generated/meshtastic/deviceonly.pb.h"

class FlashSlotStore
{
  public:
    static constexpr uint16_t MANIFEST_MAGIC = 0x4e44;
    static constexpr uint16_t RECORD_MAGIC = 0x4e53;
    static constexpr uint8_t FORMAT_VERSION = 1;
    static constexpr uint8_t RECORD_FLAG_PRESENT = 0x01;
    static constexpr uint16_t DEFAULT_SLOTS_PER_PAGE = 8;
    static constexpr const char *DEFAULT_DIRECTORY = "/prefs/nodedb";

    struct __attribute__((packed)) RecordHeader {
        uint16_t magic = RECORD_MAGIC;
        uint8_t version = FORMAT_VERSION;
        uint8_t flags = 0;
        uint16_t encoded_len = 0;
        uint16_t reserved = 0;
        uint32_t crc32 = 0;
    };

    struct __attribute__((packed)) Record {
        RecordHeader header;
        uint8_t payload[meshtastic_NodeInfoLite_size] = {};
    };

    struct __attribute__((packed)) Manifest {
        uint16_t magic = MANIFEST_MAGIC;
        uint8_t version = FORMAT_VERSION;
        uint8_t flags = 0;
        uint16_t slot_count = 0;
        uint16_t record_size = sizeof(Record);
        uint16_t slots_per_page = DEFAULT_SLOTS_PER_PAGE;
        uint16_t reserved = 0;
    };

    explicit FlashSlotStore(const char *directory = DEFAULT_DIRECTORY);

    bool initialize(uint16_t slotCount, uint16_t slotsPerPage = DEFAULT_SLOTS_PER_PAGE) const;
    bool readManifest(Manifest &manifest) const;
    bool writeManifest(const Manifest &manifest) const;
    bool readSlot(uint16_t slotIndex, meshtastic_NodeInfoLite &node) const;
    bool writeSlot(uint16_t slotIndex, const meshtastic_NodeInfoLite &node) const;
    bool clearSlot(uint16_t slotIndex) const;

  private:
    const char *directory;
};

static_assert(sizeof(FlashSlotStore::Manifest) == 12, "FlashSlotStore manifest must stay fixed-width.");
static_assert(sizeof(FlashSlotStore::RecordHeader) == 12, "FlashSlotStore record header must stay fixed-width.");
static_assert(sizeof(FlashSlotStore::Record) == (sizeof(FlashSlotStore::RecordHeader) + meshtastic_NodeInfoLite_size),
              "FlashSlotStore record layout must stay packed.");
