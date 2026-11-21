#pragma once

#include <pb.h>

// Fields marked (meshtastic.field_metadata).sensitive in field_metadata_registry.h, found anywhere in a nanopb struct.
// Blank means the field's default: has_ false, empty string or bytes, zero, or no callback.
namespace SensitiveFields
{

/// Whether field `tag` of message type `desc` is marked sensitive.
bool isSensitive(const pb_msgdesc_t *desc, pb_size_t tag);

/// Blank every sensitive field in msg. Returns true if any of them held a value.
bool redact(const pb_msgdesc_t *desc, void *msg);

/// Whether any sensitive field in msg holds a value.
bool anySet(const pb_msgdesc_t *desc, const void *msg);

/// Copy each blank sensitive field in msg from the same field of stored (same type as msg).
void restore(const pb_msgdesc_t *desc, void *msg, const void *stored);

/// restore() for a message carrying one section of a store in a oneof (Config for LocalConfig,
/// ModuleConfig for LocalModuleConfig): the stored section is the field of store with the same type.
void restoreSection(const pb_msgdesc_t *desc, void *msg, const pb_msgdesc_t *storeDesc, const void *store);

/// The descriptor for a registry message type ("meshtastic.Config.NetworkConfig"), or nullptr if
/// this firmware cannot resolve it. Every registry entry must resolve, or its field goes unprotected.
const pb_msgdesc_t *descriptorFor(const char *protoName);

} // namespace SensitiveFields
