#include "fw_manager.h"
#include "config.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <zlib.h>

volatile FwState g_fw_state = FwState::EMPTY;
static volatile bool s_extract_pending = false;

// ---------------------------------------------------------------------------
// Minimal ZIP local-file-header parser.
//
// ZIP local file header layout (all fields little-endian):
//   0  4  Signature 0x04034b50
//   4  2  Version needed
//   6  2  General purpose bit flags
//   8  2  Compression (0=STORED, 8=DEFLATED)
//  10  2  Mod time
//  12  2  Mod date
//  14  4  CRC-32
//  18  4  Compressed size
//  22  4  Uncompressed size
//  26  2  Filename length
//  28  2  Extra field length
//  30  ?  Filename
//  ..  ?  Extra field
//  ..  ?  File data
//
// We only handle STORED and DEFLATED (the two used by Nordic DFU zips).
// Data descriptors (bit 3 of flags) are not supported — DFU zips don't use them.
// ---------------------------------------------------------------------------

static uint16_t readU16(File &f)
{
    uint8_t b[2];
    f.read(b, 2);
    return (uint16_t)(b[0] | (b[1] << 8));
}

static uint32_t readU32(File &f)
{
    uint8_t b[4];
    f.read(b, 4);
    return (uint32_t)(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

// Copy `size` bytes from src (File) to dest_path in LittleFS.
static bool copyStored(File &src, const char *dest_path, uint32_t size)
{
    File out = LittleFS.open(dest_path, "w");
    if (!out) return false;

    uint8_t buf[512];
    uint32_t rem = size;
    while (rem > 0) {
        uint32_t n = min(rem, (uint32_t)sizeof(buf));
        int r = src.read(buf, n);
        if (r <= 0) { out.close(); return false; }
        out.write(buf, r);
        rem -= r;
    }
    out.close();
    return true;
}

// Inflate (raw DEFLATE, no zlib header) `comp_size` bytes from src to dest_path.
static bool inflateToFile(File &src, const char *dest_path,
                           uint32_t comp_size, uint32_t uncomp_size)
{
    File out = LittleFS.open(dest_path, "w");
    if (!out) return false;

    z_stream strm = {};
    // -MAX_WBITS: raw DEFLATE without zlib wrapper (ZIP uses raw DEFLATE)
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
        out.close();
        return false;
    }

    uint8_t in_buf[512];
    uint8_t out_buf[1024];
    uint32_t rem = comp_size;
    int zret     = Z_OK;

    while (rem > 0 && zret != Z_STREAM_END) {
        uint32_t to_read = min(rem, (uint32_t)sizeof(in_buf));
        int n = src.read(in_buf, to_read);
        if (n <= 0) break;
        rem -= n;

        strm.next_in  = in_buf;
        strm.avail_in = n;

        do {
            strm.next_out  = out_buf;
            strm.avail_out = sizeof(out_buf);
            zret = inflate(&strm, Z_NO_FLUSH);
            if (zret == Z_STREAM_ERROR || zret == Z_DATA_ERROR || zret == Z_MEM_ERROR) {
                Serial.printf("[ZIP] inflate error %d\n", zret);
                inflateEnd(&strm);
                out.close();
                return false;
            }
            size_t have = sizeof(out_buf) - strm.avail_out;
            out.write(out_buf, have);
        } while (strm.avail_in > 0);
    }

    inflateEnd(&strm);
    out.close();

    // Verify decompressed size
    File check = LittleFS.open(dest_path, "r");
    size_t actual = check.size();
    check.close();
    if (actual != uncomp_size) {
        Serial.printf("[ZIP] Size mismatch: expected %u got %u\n", uncomp_size, (unsigned)actual);
        return false;
    }
    return true;
}

static bool endsWith(const char *str, const char *suffix)
{
    size_t sl = strlen(str), xl = strlen(suffix);
    if (xl > sl) return false;
    return strcmp(str + sl - xl, suffix) == 0;
}

// Parse the ZIP and extract the first .dat and first .bin entry found.
static bool extractZip(const char *zip_path, const char *dat_out, const char *bin_out)
{
    File zip = LittleFS.open(zip_path, "r");
    if (!zip) {
        Serial.println("[ZIP] Cannot open zip");
        return false;
    }

    bool got_dat = false, got_bin = false;

    while (zip.available() >= 4) {
        uint32_t sig = readU32(zip);
        if (sig != 0x04034b50) break; // no more local file headers

        /* uint16_t version  = */ readU16(zip);
        uint16_t flags       = readU16(zip);
        uint16_t compression = readU16(zip);
        /* uint16_t mod_time = */ readU16(zip);
        /* uint16_t mod_date = */ readU16(zip);
        /* uint32_t crc32    = */ readU32(zip);
        uint32_t comp_size   = readU32(zip);
        uint32_t uncomp_size = readU32(zip);
        uint16_t fname_len   = readU16(zip);
        uint16_t extra_len   = readU16(zip);

        if (flags & 0x08) {
            // Data descriptor present — sizes in header are 0; not supported
            Serial.println("[ZIP] Data descriptor not supported");
            zip.close();
            return false;
        }

        char fname[256] = {};
        size_t to_read = fname_len < sizeof(fname) - 1 ? fname_len : sizeof(fname) - 1;
        zip.read((uint8_t *)fname, to_read);
        // Skip any remaining filename bytes + extra field
        zip.seek(zip.position() + (fname_len - to_read) + extra_len);

        Serial.printf("[ZIP] Entry: %s (%u -> %u bytes, method=%u)\n",
                      fname, comp_size, uncomp_size, compression);

        const char *out_path = nullptr;
        bool *flag = nullptr;
        if (!got_dat && endsWith(fname, ".dat")) { out_path = dat_out; flag = &got_dat; }
        else if (!got_bin && endsWith(fname, ".bin")) { out_path = bin_out; flag = &got_bin; }

        if (out_path) {
            bool ok = false;
            if (compression == 0) {
                ok = copyStored(zip, out_path, comp_size);
            } else if (compression == 8) {
                ok = inflateToFile(zip, out_path, comp_size, uncomp_size);
            } else {
                Serial.printf("[ZIP] Unsupported compression %u\n", compression);
                zip.seek(zip.position() + comp_size);
            }
            if (ok) {
                *flag = true;
                Serial.printf("[ZIP] Extracted %s (%u bytes)\n", out_path, uncomp_size);
            } else {
                Serial.printf("[ZIP] Failed to extract %s\n", fname);
                zip.close();
                return false;
            }
        } else {
            zip.seek(zip.position() + comp_size); // skip
        }

        if (got_dat && got_bin) break; // done
    }

    zip.close();

    if (!got_dat) Serial.println("[ZIP] .dat not found in zip");
    if (!got_bin) Serial.println("[ZIP] .bin not found in zip");
    return got_dat && got_bin;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void fwManagerUploadDone()
{
    s_extract_pending = true;
    g_fw_state        = FwState::EXTRACTING;
}

void fwManagerTick()
{
    if (!s_extract_pending) return;
    s_extract_pending = false;

    Serial.println("[FW] Extracting zip...");

    // Remove stale files before extraction
    LittleFS.remove(FW_DAT_PATH);
    LittleFS.remove(FW_BIN_PATH);

    if (extractZip(ZIP_TMP_PATH, FW_DAT_PATH, FW_BIN_PATH)) {
        g_fw_state = FwState::READY;
        Serial.printf("[FW] Ready — DAT %u B, BIN %u B\n",
                      (unsigned)fwDatSize(), (unsigned)fwBinSize());
    } else {
        g_fw_state = FwState::ERROR;
        Serial.println("[FW] Extraction failed");
    }

    LittleFS.remove(ZIP_TMP_PATH);
}

size_t fwDatSize()
{
    File f = LittleFS.open(FW_DAT_PATH, "r");
    if (!f) return 0;
    size_t s = f.size();
    f.close();
    return s;
}

size_t fwBinSize()
{
    File f = LittleFS.open(FW_BIN_PATH, "r");
    if (!f) return 0;
    size_t s = f.size();
    f.close();
    return s;
}
