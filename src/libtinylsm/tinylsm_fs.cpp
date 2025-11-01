#include "tinylsm_fs.h"
#include "FSCommon.h"
#include "configuration.h"
#include <cstdlib>
#include <cstring>

#if defined(ARCH_ESP32)
#include <FS.h>
#include <LittleFS.h>
#define FS_IMPL LittleFS
// FILE_O_WRITE is defined in FSCommon.h for ESP32
#elif defined(ARCH_NRF52)
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
using namespace Adafruit_LittleFS_Namespace;
#define FS_IMPL InternalFS
#ifndef FILE_O_WRITE
#define FILE_O_WRITE 1 // uint8_t mode, not string
#endif
#ifndef FILE_O_READ
#define FILE_O_READ 0 // uint8_t mode, not string
#endif
#elif defined(ARCH_RP2040)
#include <FS.h>
#include <LittleFS.h>
#define FS_IMPL LittleFS
#ifndef FILE_O_WRITE
#define FILE_O_WRITE FILE_O_WRITE
#endif
#elif defined(ARCH_STM32WL)
#include "LittleFS.h"
#include "STM32_LittleFS.h"
using namespace STM32_LittleFS_Namespace;
#define FS_IMPL InternalFS
#ifndef FILE_O_WRITE
#define FILE_O_WRITE 1 // uint8_t mode, same as nRF52
#endif
#ifndef FILE_O_READ
#define FILE_O_READ 0 // uint8_t mode, same as nRF52
#endif
#elif defined(ARCH_PORTDUINO)
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
// Portduino uses POSIX filesystem
#ifndef FILE_O_WRITE
#define FILE_O_WRITE "w"
#endif
#else
#error "Unsupported platform for LittleFS"
#endif

namespace meshtastic
{
namespace tinylsm
{

#if !defined(ARCH_PORTDUINO)
// Arduino File wrapper (type-erased to avoid template issues)
struct FileWrapper {
#if defined(ARCH_ESP32)
    fs::File file;
#elif defined(ARCH_NRF52)
    // nRF52 File requires filesystem reference, so we use a pointer
    // and allocate with placement new
    Adafruit_LittleFS_Namespace::File *file;
    char file_storage[sizeof(Adafruit_LittleFS_Namespace::File)];
#elif defined(ARCH_RP2040)
    fs::File file;
#elif defined(ARCH_STM32WL)
    // STM32WL File similar to nRF52 - requires filesystem reference
    STM32_LittleFS_Namespace::File *file;
    char file_storage[sizeof(STM32_LittleFS_Namespace::File)];
#endif

    ~FileWrapper()
    {
#if defined(ARCH_NRF52)
        if (file) {
            file->~File();
            file = nullptr;
        }
#elif defined(ARCH_STM32WL)
        if (file) {
            file->~File();
            file = nullptr;
        }
#endif
    }
};
#endif

// ============================================================================
// FileHandle Implementation
// ============================================================================

#if defined(ARCH_PORTDUINO)
FileHandle::FileHandle() : fp(nullptr), is_open(false) {}
#else
FileHandle::FileHandle() : file_obj(nullptr), is_open(false) {}
#endif

FileHandle::FileHandle(FileHandle &&other) noexcept : is_open(other.is_open)
{
#if defined(ARCH_PORTDUINO)
    fp = other.fp;
    other.fp = nullptr;
#else
    file_obj = other.file_obj;
    other.file_obj = nullptr;
#endif
    other.is_open = false;
}

FileHandle &FileHandle::operator=(FileHandle &&other) noexcept
{
    if (this != &other) {
        close();
#if defined(ARCH_PORTDUINO)
        fp = other.fp;
        other.fp = nullptr;
#else
        file_obj = other.file_obj;
        other.file_obj = nullptr;
#endif
        is_open = other.is_open;
        other.is_open = false;
    }
    return *this;
}

bool FileHandle::open(const char *path, const char *mode)
{
    close();

#if defined(ARCH_PORTDUINO)
    // POSIX file operations
    fp = fopen(path, mode);
    if (fp) {
        is_open = true;
        LOG_DEBUG("FileHandle: Opened %s", path);
        return true;
    }
    LOG_WARN("FileHandle: Failed to open %s in mode '%s'", path, mode);
    return false;
#else
    // Use Arduino File API for LittleFS
    FileWrapper *wrapper = new FileWrapper();
    if (!wrapper) {
        LOG_ERROR("FileHandle: Out of memory for wrapper");
        return false;
    }

#if defined(ARCH_NRF52) || defined(ARCH_STM32WL)
    // Initialize file pointer
    wrapper->file = nullptr;
#endif

    // Convert stdio mode strings to Arduino File modes
#if defined(ARCH_NRF52) || defined(ARCH_STM32WL)
    // nRF52 and STM32WL use uint8_t modes
    uint8_t arduino_mode;
    if (strcmp(mode, "wb") == 0 || strcmp(mode, "w") == 0) {
        arduino_mode = FILE_O_WRITE;
    } else if (strcmp(mode, "rb") == 0 || strcmp(mode, "r") == 0) {
        arduino_mode = FILE_O_READ;
    } else if (strcmp(mode, "ab") == 0 || strcmp(mode, "a") == 0) {
        arduino_mode = FILE_O_WRITE; // Append = write mode
    } else {
        delete wrapper;
        LOG_WARN("FileHandle: Unknown mode '%s'", mode);
        return false;
    }

    // For nRF52/STM32WL, use placement new to construct File from open() result
#if defined(ARCH_NRF52)
    Adafruit_LittleFS_Namespace::File opened_file = FS_IMPL.open(path, arduino_mode);
    if (opened_file) {
        wrapper->file = new (wrapper->file_storage) Adafruit_LittleFS_Namespace::File(opened_file);
        file_obj = wrapper;
        is_open = true;
        LOG_DEBUG("FileHandle: Opened %s in mode '%s' (size=%u)", path, mode, wrapper->file->size());

        // For append mode, seek to end
        if (strcmp(mode, "ab") == 0 || strcmp(mode, "a") == 0) {
            // nRF52 seek() only takes position, so get size and seek to it
            long file_size = wrapper->file->size();
            wrapper->file->seek(file_size >= 0 ? static_cast<uint32_t>(file_size) : 0);
        }
        return true;
    } else {
        delete wrapper;
        LOG_WARN("FileHandle: Failed to open %s in mode '%s' (filesystem mounted?)", path, mode);
        return false;
    }
#elif defined(ARCH_STM32WL)
    STM32_LittleFS_Namespace::File opened_file = FS_IMPL.open(path, arduino_mode);
    if (opened_file) {
        wrapper->file = new (wrapper->file_storage) STM32_LittleFS_Namespace::File(opened_file);
        file_obj = wrapper;
        is_open = true;
        LOG_DEBUG("FileHandle: Opened %s in mode '%s' (size=%u)", path, mode, wrapper->file->size());

        // For append mode, seek to end (STM32WL seek() takes position only)
        if (strcmp(mode, "ab") == 0 || strcmp(mode, "a") == 0) {
            long file_size = wrapper->file->size();
            wrapper->file->seek(file_size >= 0 ? static_cast<uint32_t>(file_size) : 0);
        }
        return true;
    } else {
        delete wrapper;
        LOG_WARN("FileHandle: Failed to open %s in mode '%s' (filesystem mounted?)", path, mode);
        return false;
    }
#endif
#else
    // ESP32/RP2040 use string modes
    const char *arduino_mode = mode;
    if (strcmp(mode, "wb") == 0 || strcmp(mode, "w") == 0) {
        arduino_mode = FILE_O_WRITE;
    } else if (strcmp(mode, "rb") == 0 || strcmp(mode, "r") == 0) {
        arduino_mode = FILE_O_READ;
    } else if (strcmp(mode, "ab") == 0 || strcmp(mode, "a") == 0) {
        arduino_mode = FILE_O_WRITE; // Append = write mode
    }

    wrapper->file = FS_IMPL.open(path, arduino_mode);
    if (wrapper->file) {
        file_obj = wrapper;
        is_open = true;
        LOG_DEBUG("FileHandle: Opened %s in mode '%s' (size=%u)", path, mode, wrapper->file.size());

        // For append mode, seek to end
        if (strcmp(mode, "ab") == 0 || strcmp(mode, "a") == 0) {
            wrapper->file.seek(0, fs::SeekEnd);
        }
        return true;
    } else {
        delete wrapper;
        LOG_WARN("FileHandle: Failed to open %s in mode '%s' (filesystem mounted?)", path, mode);
        return false;
    }
#endif
#endif
}

bool FileHandle::close()
{
#if defined(ARCH_PORTDUINO)
    if (is_open && fp) {
        fclose(fp);
        fp = nullptr;
        is_open = false;
        return true;
    }
#else
    if (is_open && file_obj) {
        FileWrapper *wrapper = static_cast<FileWrapper *>(file_obj);
#if defined(ARCH_NRF52) || defined(ARCH_STM32WL)
        if (wrapper->file) {
            wrapper->file->close();
        }
#else
        wrapper->file.close();
#endif
        delete wrapper;
        file_obj = nullptr;
        is_open = false;
        return true;
    }
#endif
    return false;
}

size_t FileHandle::read(void *buffer, size_t size)
{
#if defined(ARCH_PORTDUINO)
    if (!is_open || !fp)
        return 0;
    return fread(buffer, 1, size, fp);
#else
    if (!is_open || !file_obj)
        return 0;
    FileWrapper *wrapper = static_cast<FileWrapper *>(file_obj);
#if defined(ARCH_NRF52) || defined(ARCH_STM32WL)
    return wrapper->file ? wrapper->file->read(static_cast<uint8_t *>(buffer), size) : 0;
#else
    return wrapper->file.read(static_cast<uint8_t *>(buffer), size);
#endif
#endif
}

size_t FileHandle::write(const void *buffer, size_t size)
{
#if defined(ARCH_PORTDUINO)
    if (!is_open || !fp)
        return 0;
    return fwrite(buffer, 1, size, fp);
#else
    if (!is_open || !file_obj)
        return 0;
    FileWrapper *wrapper = static_cast<FileWrapper *>(file_obj);
#if defined(ARCH_NRF52) || defined(ARCH_STM32WL)
    return wrapper->file ? wrapper->file->write(static_cast<const uint8_t *>(buffer), size) : 0;
#else
    return wrapper->file.write(static_cast<const uint8_t *>(buffer), size);
#endif
#endif
}

bool FileHandle::seek(long offset, int whence)
{
#if defined(ARCH_PORTDUINO)
    if (!is_open || !fp)
        return false;
    return fseek(fp, offset, whence) == 0;
#else
    if (!is_open || !file_obj)
        return false;
    FileWrapper *wrapper = static_cast<FileWrapper *>(file_obj);

    // Arduino File uses SeekMode enum
#if defined(ARCH_NRF52) || defined(ARCH_STM32WL)
    // nRF52/STM32WL File API: seek() only takes position, not offset+whence
    // Calculate absolute position based on whence
    uint32_t abs_pos;
    if (whence == SEEK_SET) {
        abs_pos = (offset >= 0) ? static_cast<uint32_t>(offset) : 0;
    } else if (whence == SEEK_CUR) {
        long current = wrapper->file ? wrapper->file->position() : 0;
        abs_pos = (offset >= 0)                                 ? static_cast<uint32_t>(current + offset)
                  : (current >= static_cast<uint32_t>(-offset)) ? static_cast<uint32_t>(current + offset)
                                                                : 0;
    } else if (whence == SEEK_END) {
        long file_size = wrapper->file ? wrapper->file->size() : 0;
        abs_pos = (offset >= 0)                                   ? static_cast<uint32_t>(file_size + offset)
                  : (file_size >= static_cast<uint32_t>(-offset)) ? static_cast<uint32_t>(file_size + offset)
                                                                  : 0;
    } else {
        return false;
    }
    return wrapper->file ? wrapper->file->seek(abs_pos) : false;
#else
    fs::SeekMode mode;
    if (whence == SEEK_SET)
        mode = fs::SeekSet;
    else if (whence == SEEK_CUR)
        mode = fs::SeekCur;
    else if (whence == SEEK_END)
        mode = fs::SeekEnd;
    else
        return false;
    return wrapper->file.seek(offset, mode);
#endif
#endif
}

long FileHandle::tell()
{
#if defined(ARCH_PORTDUINO)
    if (!is_open || !fp)
        return -1;
    return ftell(fp);
#else
    if (!is_open || !file_obj)
        return -1;
    FileWrapper *wrapper = static_cast<FileWrapper *>(file_obj);
#if defined(ARCH_NRF52) || defined(ARCH_STM32WL)
    return wrapper->file ? wrapper->file->position() : -1;
#else
    return wrapper->file.position();
#endif
#endif
}

bool FileHandle::rewind()
{
#if defined(ARCH_PORTDUINO)
    if (!is_open || !fp)
        return false;
    ::rewind(fp);
    return true;
#else
    return seek(0, SEEK_SET);
#endif
}

long FileHandle::size()
{
#if defined(ARCH_PORTDUINO)
    if (!is_open || !fp)
        return -1;
    long current = tell();
    seek(0, SEEK_END);
    long sz = tell();
    seek(current, SEEK_SET);
    return sz;
#else
    if (!is_open || !file_obj)
        return -1;
    FileWrapper *wrapper = static_cast<FileWrapper *>(file_obj);
#if defined(ARCH_NRF52) || defined(ARCH_STM32WL)
    return wrapper->file ? wrapper->file->size() : -1;
#else
    return wrapper->file.size();
#endif
#endif
}

bool FileHandle::sync()
{
#if defined(ARCH_PORTDUINO)
    if (!is_open || !fp)
        return false;
    fflush(fp);
    fsync(fileno(fp));
    return true;
#else
    if (!is_open || !file_obj)
        return false;
    FileWrapper *wrapper = static_cast<FileWrapper *>(file_obj);
#if defined(ARCH_NRF52) || defined(ARCH_STM32WL)
    if (wrapper->file) {
        wrapper->file->flush();
    }
#else
    wrapper->file.flush();
#endif
    return true;
#endif
}

// ============================================================================
// FileSystem Implementation
// ============================================================================

bool FileSystem::mounted = false;

bool FileSystem::init(const char *base_path)
{
    if (mounted) {
        return true;
    }

#if defined(ARCH_PORTDUINO)
    // POSIX filesystem, create directory if needed
    FileSystem::mkdir(base_path);
    mounted = true;
    return true;
#else
    // Check if filesystem is already mounted by Meshtastic
    // FSBegin() should have been called earlier in main.cpp
#if defined(ARCH_ESP32)
    mounted = LittleFS.begin(true); // format on failure
#elif defined(ARCH_NRF52)
    mounted = InternalFS.begin();
#elif defined(ARCH_RP2040)
    mounted = LittleFS.begin();
#elif defined(ARCH_STM32WL)
    mounted = InternalFS.begin();
#else
    mounted = FSBegin();
#endif

    if (!mounted) {
        LOG_ERROR("FileSystem: Failed to mount LittleFS");
        return false;
    }

    LOG_DEBUG("FileSystem: LittleFS mounted successfully");

    // Create base directory
    if (base_path && strlen(base_path) > 0) {
        if (!mkdir(base_path)) {
            LOG_WARN("FileSystem: Failed to create directory %s (may already exist)", base_path);
            // Don't fail - directory might already exist
        }
    }

    return mounted;
#endif
}

bool FileSystem::is_mounted()
{
    return mounted;
}

bool FileSystem::mkdir(const char *path)
{
#if defined(ARCH_PORTDUINO)
    return ::mkdir(path, 0755) == 0 || errno == EEXIST;
#else
    // LittleFS mkdir (Arduino style)
    return FS_IMPL.mkdir(path);
#endif
}

bool FileSystem::exists(const char *path)
{
#if defined(ARCH_PORTDUINO)
    struct stat st;
    return stat(path, &st) == 0;
#else
    return FS_IMPL.exists(path);
#endif
}

bool FileSystem::is_directory(const char *path)
{
#if defined(ARCH_PORTDUINO)
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
#else
    // Arduino LittleFS doesn't have direct is_dir check
    // Try to open as directory
#if defined(ARCH_RP2040)
    // RP2040 requires mode parameter
    auto dir = FS_IMPL.open(path, FILE_O_READ);
#else
    auto dir = FS_IMPL.open(path);
#endif
    if (!dir) {
        return false;
    }
    bool is_dir = dir.isDirectory();
    dir.close();
    return is_dir;
#endif
}

bool FileSystem::remove(const char *path)
{
#if defined(ARCH_PORTDUINO)
    return ::remove(path) == 0;
#else
    return FS_IMPL.remove(path);
#endif
}

bool FileSystem::rename(const char *old_path, const char *new_path)
{
#if defined(ARCH_PORTDUINO)
    return ::rename(old_path, new_path) == 0;
#else
    return FS_IMPL.rename(old_path, new_path);
#endif
}

bool FileSystem::atomic_write(const char *final_path, const void *data, size_t size)
{
    // Build temp path
    char temp_path[constants::MAX_PATH];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", final_path);

    // Write to temp file
    FileHandle fh;
    if (!fh.open(temp_path, "wb")) {
        LOG_ERROR("Failed to open temp file: %s", temp_path);
        return false;
    }

    size_t written = fh.write(data, size);
    if (written != size) {
        LOG_ERROR("Failed to write temp file: %s (wrote %u of %u bytes)", temp_path, written, size);
        fh.close();
        remove(temp_path);
        return false;
    }

    // Sync
    if (!fh.sync()) {
        LOG_ERROR("Failed to sync temp file: %s", temp_path);
        fh.close();
        remove(temp_path);
        return false;
    }

    fh.close();

    // Atomic rename
    if (!rename(temp_path, final_path)) {
        LOG_ERROR("Failed to rename %s to %s", temp_path, final_path);
        remove(temp_path);
        return false;
    }

    return true;
}

bool FileSystem::atomic_write_ab(const char *base_name, bool use_a, const void *data, size_t size)
{
    char path[constants::MAX_PATH];
    if (!PathUtil::build_ab_path(path, sizeof(path), nullptr, base_name, use_a)) {
        return false;
    }
    return atomic_write(path, data, size);
}

bool FileSystem::read_ab(const char *base_name, bool *which_valid, void **data, size_t *size)
{
    char path_a[constants::MAX_PATH];
    char path_b[constants::MAX_PATH];

    if (!PathUtil::build_ab_path(path_a, sizeof(path_a), nullptr, base_name, true) ||
        !PathUtil::build_ab_path(path_b, sizeof(path_b), nullptr, base_name, false)) {
        return false;
    }

    bool a_exists = exists(path_a);
    bool b_exists = exists(path_b);

    if (!a_exists && !b_exists) {
        return false;
    }

    // Try to read both, prefer the one that reads successfully
    // In case of both valid, prefer A (arbitrary choice)
    const char *path_to_read = a_exists ? path_a : path_b;
    *which_valid = a_exists;

    FileHandle fh;
    if (!fh.open(path_to_read, "rb")) {
        LOG_ERROR("Failed to open %s", path_to_read);
        return false;
    }

    long file_size = fh.size();
    if (file_size <= 0) {
        LOG_ERROR("Invalid file size for %s", path_to_read);
        return false;
    }

    *data = malloc(file_size);
    if (!*data) {
        LOG_ERROR("Failed to allocate %ld bytes for %s", file_size, path_to_read);
        return false;
    }

    size_t read_bytes = fh.read(*data, file_size);
    if (read_bytes != (size_t)file_size) {
        LOG_ERROR("Failed to read %s", path_to_read);
        free(*data);
        *data = nullptr;
        return false;
    }

    *size = file_size;
    fh.close();
    return true;
}

bool FileSystem::list_files(const char *dir_path, file_callback_t callback, void *user_data)
{
#if defined(ARCH_PORTDUINO)
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        callback(entry->d_name, user_data);
    }

    closedir(dir);
    return true;
#else
#if defined(ARCH_RP2040)
    // RP2040 requires mode parameter
    auto dir = FS_IMPL.open(dir_path, FILE_O_READ);
#else
    auto dir = FS_IMPL.open(dir_path);
#endif
    if (!dir) {
        return false;
    }

    auto file = dir.openNextFile();
    while (file) {
        const char *name = file.name();
        callback(name, user_data);
        file.close();
        file = dir.openNextFile();
    }

    dir.close();
    return true;
#endif
}

size_t FileSystem::free_space()
{
#if defined(ARCH_PORTDUINO)
    // Not easily available on POSIX
    return 1024 * 1024 * 100; // Assume 100MB
#elif defined(ARCH_ESP32)
    return LittleFS.totalBytes() - LittleFS.usedBytes();
#else
    // Not easily available on all platforms
    return 0;
#endif
}

size_t FileSystem::total_space()
{
#if defined(ARCH_PORTDUINO)
    return 1024 * 1024 * 100; // Assume 100MB
#elif defined(ARCH_ESP32)
    return LittleFS.totalBytes();
#else
    return 0;
#endif
}

// ============================================================================
// PathUtil Implementation
// ============================================================================

bool PathUtil::build_path(char *dest, size_t dest_size, const char *base, const char *name)
{
    if (!base || !name) {
        return false;
    }
    int written = snprintf(dest, dest_size, "%s/%s", base, name);
    return written > 0 && (size_t)written < dest_size;
}

bool PathUtil::build_temp_path(char *dest, size_t dest_size, const char *base, const char *name)
{
    if (!base || !name) {
        return false;
    }
    int written = snprintf(dest, dest_size, "%s/%s.tmp", base, name);
    return written > 0 && (size_t)written < dest_size;
}

bool PathUtil::build_ab_path(char *dest, size_t dest_size, const char *base, const char *name, bool use_a)
{
    if (!name) {
        return false;
    }
    int written;
    if (base) {
        written = snprintf(dest, dest_size, "%s/%s-%c", base, name, use_a ? 'A' : 'B');
    } else {
        written = snprintf(dest, dest_size, "%s-%c", name, use_a ? 'A' : 'B');
    }
    return written > 0 && (size_t)written < dest_size;
}

const char *PathUtil::filename(const char *path)
{
    const char *last_slash = strrchr(path, '/');
    return last_slash ? last_slash + 1 : path;
}

bool PathUtil::dirname(char *dest, size_t dest_size, const char *path)
{
    const char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        dest[0] = '.';
        dest[1] = '\0';
        return true;
    }

    size_t len = last_slash - path;
    if (len >= dest_size) {
        return false;
    }

    memcpy(dest, path, len);
    dest[len] = '\0';
    return true;
}

} // namespace tinylsm
} // namespace meshtastic
