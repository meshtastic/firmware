// InternalFileSystem.h — Zephyr LittleFS backend for nRF54L15
//
// Implements the Adafruit InternalFileSystem API subset used by Meshtastic,
// backed by Zephyr's fs/littlefs on the 36 KB storage_partition of the
// nRF54L15's internal RRAM.
//
// Mount point: /lfs
// All paths passed to open/exists/mkdir etc. are relative to the FS root
// (e.g. "/prefs/config.proto") and are prepended with "/lfs" internally.
//
// File objects are copyable via std::shared_ptr<NRF54L15FileState>.
// The underlying Zephyr handle is closed when the last copy is destroyed.

#pragma once

#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/fs/fs.h>

#ifndef FILE_O_READ
#define FILE_O_READ "r"
#define FILE_O_WRITE "w"
#endif

#define NRF54L15_FS_MOUNT "/lfs"
#define NRF54L15_FS_PATHLEN 256

namespace Adafruit_LittleFS_Namespace
{

class InternalFileSystem; // forward

// ── Internal file/dir state ───────────────────────────────────────────────

struct NRF54L15FileState {
    bool valid = false;
    bool is_dir = false;

    // Absolute Zephyr path, e.g. "/lfs/prefs/config.proto"
    char fullpath[NRF54L15_FS_PATHLEN] = {0};
    // Path from FS root, e.g. "/prefs/config.proto"  (returned by name())
    char relpath[NRF54L15_FS_PATHLEN] = {0};

    struct fs_file_t file;
    struct fs_dir_t dir;

    NRF54L15FileState()
    {
        fs_file_t_init(&file);
        fs_dir_t_init(&dir);
    }

    ~NRF54L15FileState()
    {
        if (valid) {
            if (is_dir)
                fs_closedir(&dir);
            else
                fs_close(&file);
            valid = false;
        }
    }
};

// ── File ─────────────────────────────────────────────────────────────────

class File
{
  public:
    File() = default;
    explicit File(InternalFileSystem &) {} // nRF52 compat constructor

    explicit operator bool() const { return _s && _s->valid; }

    int read(void *buf, uint16_t nbyte)
    {
        if (!_s || !_s->valid || _s->is_dir)
            return -1;
        ssize_t n = fs_read(&_s->file, buf, nbyte);
        return n < 0 ? -1 : (int)n;
    }

    int read()
    {
        uint8_t b;
        return read(&b, 1) == 1 ? (int)b : -1;
    }

    size_t write(const uint8_t *buf, size_t len)
    {
        if (!_s || !_s->valid || _s->is_dir)
            return 0;
        ssize_t n = fs_write(&_s->file, buf, len);
        return n < 0 ? 0 : (size_t)n;
    }

    size_t write(uint8_t b) { return write(&b, 1); }

    void flush()
    {
        if (_s && _s->valid && !_s->is_dir)
            fs_sync(&_s->file);
    }

    void close() { _s.reset(); }

    size_t size()
    {
        if (!_s || !_s->valid || _s->is_dir)
            return 0;
        struct fs_dirent entry;
        if (fs_stat(_s->fullpath, &entry) == 0)
            return (size_t)entry.size;
        return 0;
    }

    bool isDirectory() { return _s && _s->valid && _s->is_dir; }

    // Returns path from FS root, e.g. "/prefs/config.proto"
    const char *name() { return _s ? _s->relpath : ""; }

    // Returns the next entry in a directory.  Modifies the dir stream in _s.
    File openNextFile();

    void rewindDirectory()
    {
        if (_s && _s->valid && _s->is_dir)
            fs_opendir(&_s->dir, _s->fullpath);
    }

    bool seek(uint32_t pos)
    {
        if (!_s || !_s->valid || _s->is_dir)
            return false;
        return fs_seek(&_s->file, (off_t)pos, FS_SEEK_SET) == 0;
    }

    int available()
    {
        if (!_s || !_s->valid || _s->is_dir)
            return 0;
        off_t pos = fs_tell(&_s->file);
        if (pos < 0)
            return 0;
        struct fs_dirent entry;
        if (fs_stat(_s->fullpath, &entry) != 0)
            return 0;
        long rem = (long)entry.size - (long)pos;
        return rem > 0 ? (int)rem : 0;
    }

    int peek() { return -1; }

    // Internal: constructed by InternalFileSystem and openNextFile()
    explicit File(std::shared_ptr<NRF54L15FileState> s) : _s(std::move(s)) {}

  private:
    std::shared_ptr<NRF54L15FileState> _s;
};

// ── InternalFileSystem ────────────────────────────────────────────────────

class InternalFileSystem
{
  public:
    bool begin();
    File open(const char *path, const char *mode);
    bool exists(const char *path);
    bool remove(const char *path);
    bool rename(const char *from, const char *to);
    bool mkdir(const char *path);
    bool rmdir(const char *path);
    bool rmdir_r(const char *path); // recursive delete (used by FSCommon rmDir)
    uint32_t usedBytes() { return 0; }
    uint32_t totalBytes() { return 36U * 1024U; }
    bool format();

    // Convert a FS-root-relative path to an absolute Zephyr path.
    static void toabs(const char *rel, char *abs, size_t abssz);

  private:
    bool _mounted = false;
};

} // namespace Adafruit_LittleFS_Namespace

extern Adafruit_LittleFS_Namespace::InternalFileSystem InternalFS;
