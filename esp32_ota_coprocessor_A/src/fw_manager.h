#pragma once
#include <stddef.h>

enum class FwState {
    EMPTY,       // no firmware loaded
    UPLOADING,   // zip upload in progress
    EXTRACTING,  // zip received, extraction running
    READY,       // fw.dat + fw.bin available
    ERROR,       // extraction failed
};

extern volatile FwState g_fw_state;

// ---------------------------------------------------------------------------
// Called by the upload handler to signal that the zip is complete.
// Queues extraction; the actual work happens in fwManagerTick() from loop().
// ---------------------------------------------------------------------------
void fwManagerUploadDone();

// ---------------------------------------------------------------------------
// Called from loop() — does the zip extraction if queued.
// ---------------------------------------------------------------------------
void fwManagerTick();

// Return sizes of the cached files (0 if not ready).
size_t fwDatSize();
size_t fwBinSize();
