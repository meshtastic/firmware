#include "ClientAppDataStore.h"
#include "configuration.h"
#include "gps/RTC.h"
#include "mesh/NodeDB.h"
#include <cstring>

ClientAppDataStore *clientAppDataStore = nullptr;

void ClientAppDataStore::init()
{
    if (clientAppDataStore == nullptr) {
        clientAppDataStore = new ClientAppDataStore();
    }

    if (nodeDB == nullptr) {
        // NodeDB owns the filesystem helpers; defer until it exists.
        return;
    }

    LoadFileResult state =
        nodeDB->loadProto(clientAppDataFileName, meshtastic_LocalClientAppData_size,
                          sizeof(meshtastic_LocalClientAppData), &meshtastic_LocalClientAppData_msg,
                          &clientAppDataStore->store_);

    if (state != LoadFileResult::LOAD_SUCCESS) {
        // Missing file is the expected first-boot case; only flag genuine
        // decode failures to avoid log noise on a fresh device.
        if (state == LoadFileResult::DECODE_FAILED) {
            LOG_WARN("ClientAppData: stored file failed to decode, starting empty");
        }
        memset(&clientAppDataStore->store_, 0, sizeof(meshtastic_LocalClientAppData));
    }

    if (clientAppDataStore->store_.records_count > kMaxRecords) {
        // Should never happen: nanopb caps records[4] at compile time.
        // Defensive trim in case a future schema bump changes the bound.
        clientAppDataStore->store_.records_count = kMaxRecords;
    }
}

void ClientAppDataStore::clear()
{
    if (clientAppDataStore == nullptr) {
        return;
    }
    memset(&clientAppDataStore->store_, 0, sizeof(meshtastic_LocalClientAppData));
}

bool ClientAppDataStore::isValidAppId(const char *appId)
{
    if (appId == nullptr || appId[0] == '\0') {
        return false;
    }
    size_t i = 0;
    for (; appId[i] != '\0'; i++) {
        if (i >= kMaxAppIdLen) {
            return false;
        }
        char c = appId[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        if (!ok) {
            return false;
        }
    }
    return i >= 1 && i <= kMaxAppIdLen;
}

int ClientAppDataStore::findIndex_(const char *appId) const
{
    for (pb_size_t i = 0; i < store_.records_count; i++) {
        if (strncmp(store_.records[i].app_id, appId, kMaxAppIdLen + 1) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

size_t ClientAppDataStore::recordCount() const
{
    return store_.records_count;
}

ClientAppDataStore::Result ClientAppDataStore::get(const char *appId, meshtastic_ClientAppData *out) const
{
    if (out == nullptr || !isValidAppId(appId)) {
        return Result::InvalidAppId;
    }
    int idx = findIndex_(appId);
    if (idx < 0) {
        return Result::NotFound;
    }
    *out = store_.records[idx];
    return Result::Ok;
}

ClientAppDataStore::Result ClientAppDataStore::set(const meshtastic_ClientAppData &record)
{
    if (!isValidAppId(record.app_id)) {
        LOG_WARN("ClientAppData: rejected set for invalid app_id");
        return Result::InvalidAppId;
    }
    if (record.payload.size > kMaxPayloadBytes) {
        LOG_WARN("ClientAppData: rejected set for app_id=%s, payload %u > %u", record.app_id,
                 (unsigned)record.payload.size, (unsigned)kMaxPayloadBytes);
        return Result::PayloadTooLarge;
    }

    int idx = findIndex_(record.app_id);
    if (idx < 0) {
        if (store_.records_count >= kMaxRecords) {
            LOG_WARN("ClientAppData: rejected set for app_id=%s, store full (%u/%u)", record.app_id,
                     (unsigned)store_.records_count, (unsigned)kMaxRecords);
            return Result::NoSpace;
        }
        idx = static_cast<int>(store_.records_count);
        store_.records_count++;
    }

    meshtastic_ClientAppData *slot = &store_.records[idx];
    *slot = record;
    slot->updated_at = getValidTime(RTCQualityDevice);

    if (!persistToDisk_()) {
        LOG_ERROR("ClientAppData: persist failed for app_id=%s", record.app_id);
        return Result::StorageError;
    }
    return Result::Ok;
}

ClientAppDataStore::Result ClientAppDataStore::remove(const char *appId)
{
    if (!isValidAppId(appId)) {
        LOG_WARN("ClientAppData: rejected remove for invalid app_id");
        return Result::InvalidAppId;
    }
    int idx = findIndex_(appId);
    if (idx < 0) {
        return Result::NotFound;
    }

    // Compact: shift trailing records left by one to free the slot.
    for (pb_size_t i = static_cast<pb_size_t>(idx); i + 1 < store_.records_count; i++) {
        store_.records[i] = store_.records[i + 1];
    }
    store_.records_count--;
    memset(&store_.records[store_.records_count], 0, sizeof(meshtastic_ClientAppData));

    if (!persistToDisk_()) {
        LOG_ERROR("ClientAppData: persist failed after remove for app_id=%s", appId);
        return Result::StorageError;
    }
    return Result::Ok;
}

bool ClientAppDataStore::persistToDisk_()
{
    if (nodeDB == nullptr) {
        return false;
    }
    return nodeDB->saveProto(clientAppDataFileName, meshtastic_LocalClientAppData_size,
                             &meshtastic_LocalClientAppData_msg, &store_);
}
