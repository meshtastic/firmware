#pragma once

#include "tinylsm_config.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace meshtastic
{
namespace tinylsm
{

// ============================================================================
// File Handle (wraps platform-specific FILE*)
// ============================================================================

class FileHandle
{
  private:
#if defined(ARCH_PORTDUINO)
    FILE *fp;
#else
    // Use Arduino File class for LittleFS compatibility
    void *file_obj; // Points to File object (type-erased to avoid template issues)
#endif
    bool is_open;

  public:
    FileHandle();
    ~FileHandle() { close(); }

    // Disable copy
    FileHandle(const FileHandle &) = delete;
    FileHandle &operator=(const FileHandle &) = delete;

    // Move support
    FileHandle(FileHandle &&other) noexcept;
    FileHandle &operator=(FileHandle &&other) noexcept;

    bool open(const char *path, const char *mode);
    bool close();
    bool isOpen() const { return is_open; }

    // Read/Write
    size_t read(void *buffer, size_t size);
    size_t write(const void *buffer, size_t size);

    // Seek/Tell
    bool seek(long offset, int whence);
    long tell();
    bool rewind();

    // Size
    long size();

    // Sync
    bool sync();
};

// ============================================================================
// File System Operations
// ============================================================================

class FileSystem
{
  public:
    // Initialization
    static bool init(const char *base_path);
    static bool is_mounted();

    // Directory operations
    static bool mkdir(const char *path);
    static bool exists(const char *path);
    static bool is_directory(const char *path);
    static bool remove(const char *path);
    static bool rename(const char *old_path, const char *new_path);

    // Atomic write: write to temp file, sync, then rename
    // This is the key primitive for power-loss safety
    static bool atomic_write(const char *final_path, const void *data, size_t size);

    // A/B file operations
    static bool atomic_write_ab(const char *base_name, bool use_a, const void *data, size_t size);
    static bool read_ab(const char *base_name, bool *which_valid, void **data, size_t *size);

    // List files in directory (callback-based to avoid dynamic allocation)
    typedef void (*file_callback_t)(const char *filename, void *user_data);
    static bool list_files(const char *dir_path, file_callback_t callback, void *user_data);

    // Get free space
    static size_t free_space();
    static size_t total_space();

  private:
    static bool mounted;
};

// ============================================================================
// Path Utilities
// ============================================================================

class PathUtil
{
  public:
    // Build path: base/name (no dynamic allocation)
    static bool build_path(char *dest, size_t dest_size, const char *base, const char *name);

    // Build temp path: base/name.tmp
    static bool build_temp_path(char *dest, size_t dest_size, const char *base, const char *name);

    // Build A/B paths
    static bool build_ab_path(char *dest, size_t dest_size, const char *base, const char *name, bool use_a);

    // Extract filename from path
    static const char *filename(const char *path);

    // Extract directory from path
    static bool dirname(char *dest, size_t dest_size, const char *path);
};

} // namespace tinylsm
} // namespace meshtastic
