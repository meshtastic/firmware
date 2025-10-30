// Minimal stub implementation of tinylsm_fs for native testing
// Uses POSIX file operations instead of Arduino FS
// Define ARCH_PORTDUINO to use FILE* directly
#define ARCH_PORTDUINO 1

#include "../../src/libtinylsm/tinylsm_fs.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace meshtastic
{
namespace tinylsm
{

// Minimal FileHandle implementation using POSIX
FileHandle::FileHandle() : fp(nullptr), is_open(false) {}

FileHandle::FileHandle(FileHandle &&other) noexcept : fp(other.fp), is_open(other.is_open)
{
    other.fp = nullptr;
    other.is_open = false;
}

FileHandle &FileHandle::operator=(FileHandle &&other) noexcept
{
    if (this != &other) {
        close();
        fp = other.fp;
        is_open = other.is_open;
        other.fp = nullptr;
        other.is_open = false;
    }
    return *this;
}

bool FileHandle::open(const char *path, const char *mode)
{
    close(); // Close any existing file

    // Map mode strings
    const char *c_mode = "rb";
    if (strcmp(mode, "rb") == 0 || strcmp(mode, "r") == 0) {
        c_mode = "rb";
    } else if (strcmp(mode, "wb") == 0 || strcmp(mode, "w") == 0) {
        c_mode = "wb";
    } else if (strcmp(mode, "ab") == 0 || strcmp(mode, "a") == 0) {
        c_mode = "ab";
    }

    fp = fopen(path, c_mode);
    is_open = (fp != nullptr);
    return is_open;
}

size_t FileHandle::read(void *buffer, size_t size)
{
    if (!fp)
        return 0;
    return fread(buffer, 1, size, fp);
}

size_t FileHandle::write(const void *data, size_t size)
{
    if (!fp)
        return 0;
    return fwrite(data, 1, size, fp);
}

bool FileHandle::close()
{
    if (fp) {
        fclose(fp);
        fp = nullptr;
        is_open = false;
        return true;
    }
    return false;
}

long FileHandle::size()
{
    if (!fp)
        return 0;

    long pos = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, pos, SEEK_SET);
    return sz;
}

bool FileHandle::seek(long offset, int whence)
{
    if (!fp)
        return false;
    return fseek(fp, offset, whence) == 0;
}

bool FileHandle::rewind()
{
    if (!fp)
        return false;
    ::rewind(fp);
    return true;
}

long FileHandle::tell()
{
    if (!fp)
        return 0;
    return ftell(fp);
}

bool FileHandle::sync()
{
    if (!fp)
        return false;
    return fflush(fp) == 0;
}

// Minimal FileSystem implementation using POSIX
bool FileSystem::mounted = false;

bool FileSystem::init(const char *base_path)
{
    // Create directory if it doesn't exist
    struct stat st;
    if (stat(base_path, &st) != 0) {
        // Try to create directory
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "mkdir -p %s", base_path);
        system(cmd);
    }
    mounted = true;
    return true;
}

bool FileSystem::is_mounted()
{
    return mounted;
}

bool FileSystem::exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

bool FileSystem::mkdir(const char *path)
{
    return ::mkdir(path, 0755) == 0 || errno == EEXIST;
}

bool FileSystem::is_directory(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return false;
    return S_ISDIR(st.st_mode);
}

bool FileSystem::remove(const char *path)
{
    return unlink(path) == 0 || rmdir(path) == 0;
}

bool FileSystem::rename(const char *old_path, const char *new_path)
{
    return ::rename(old_path, new_path) == 0;
}

bool FileSystem::atomic_write(const char *path, const void *data, size_t size)
{
    // Simple atomic write: write to temp file, then rename
    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);

    FILE *f = fopen(temp_path, "wb");
    if (!f)
        return false;

    size_t written = fwrite(data, 1, size, f);
    bool success = (written == size) && (fflush(f) == 0) && (fclose(f) == 0);

    if (success) {
        success = (::rename(temp_path, path) == 0);
    } else {
        unlink(temp_path);
    }

    return success;
}

bool FileSystem::atomic_write_ab(const char *base_name, bool use_a, const void *data, size_t size)
{
    char path[512];
    snprintf(path, sizeof(path), "%s%c.bin", base_name, use_a ? 'A' : 'B');
    return atomic_write(path, data, size);
}

bool FileSystem::read_ab(const char *base_name, bool *which_valid, void **data, size_t *size)
{
    char path_a[512], path_b[512];
    snprintf(path_a, sizeof(path_a), "%sA.bin", base_name);
    snprintf(path_b, sizeof(path_b), "%sB.bin", base_name);

    bool exists_a = exists(path_a);
    bool exists_b = exists(path_b);

    if (!exists_a && !exists_b)
        return false;

    const char *path = exists_a ? path_a : path_b;
    if (which_valid)
        *which_valid = exists_a;

    FILE *f = fopen(path, "rb");
    if (!f)
        return false;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    void *buf = malloc(sz);
    if (!buf) {
        fclose(f);
        return false;
    }

    if (fread(buf, 1, sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return false;
    }

    fclose(f);
    *data = buf;
    *size = sz;
    return true;
}

bool FileSystem::list_files(const char *dir_path, file_callback_t callback, void *user_data)
{
    // Simple implementation using system() - could be improved with opendir/readdir
    // For now, just return true (tests don't use this)
    (void)dir_path;
    (void)callback;
    (void)user_data;
    return true;
}

size_t FileSystem::free_space()
{
    return 100 * 1024 * 1024; // Return fake 100MB
}

size_t FileSystem::total_space()
{
    return 128 * 1024 * 1024; // Return fake 128MB
}

} // namespace tinylsm
} // namespace meshtastic
