// InternalFileSystem.cpp — Zephyr LittleFS backend for nRF54L15
//
// Implements Adafruit_LittleFS_Namespace used by FSCommon.h/cpp.
// Storage: 36 KB storage_partition in nRF54L15 internal RRAM (defined in
// zephyr/dts/nordic/nrf54l15_partition.dtsi, included by the board DTS).

#include "InternalFileSystem.h"
#include "configuration.h"

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>

using namespace Adafruit_LittleFS_Namespace;

// ── LittleFS mount ────────────────────────────────────────────────────────

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(nrf54l15_lfs_data);

static struct fs_mount_t _lfs_mnt = {
    .type = FS_LITTLEFS,
    .mnt_point = NRF54L15_FS_MOUNT,
    .fs_data = &nrf54l15_lfs_data,
    .storage_dev = (void *)(uintptr_t)FIXED_PARTITION_ID(storage_partition),
    .flags = 0,
};

// ── Global singleton ──────────────────────────────────────────────────────

Adafruit_LittleFS_Namespace::InternalFileSystem InternalFS;

// ── Path helpers ──────────────────────────────────────────────────────────

void InternalFileSystem::toabs(const char *rel, char *abs, size_t abssz)
{
    // Root "/" maps to the mount point itself (no trailing slash)
    if (rel[0] == '/' && rel[1] == '\0') {
        strncpy(abs, NRF54L15_FS_MOUNT, abssz - 1);
        abs[abssz - 1] = '\0';
    } else if (rel[0] == '/') {
        snprintf(abs, abssz, "%s%s", NRF54L15_FS_MOUNT, rel);
    } else {
        snprintf(abs, abssz, "%s/%s", NRF54L15_FS_MOUNT, rel);
    }
}

// Strip mount-point prefix to get the FS-root-relative path ("/prefs/...").
static void torel(const char *abs, char *rel, size_t relsz)
{
    const char *mp = NRF54L15_FS_MOUNT;
    size_t mplen = strlen(mp);
    if (strncmp(abs, mp, mplen) == 0) {
        const char *suffix = abs + mplen;
        if (suffix[0] == '\0') {
            strncpy(rel, "/", relsz);
        } else {
            strncpy(rel, suffix, relsz - 1);
            rel[relsz - 1] = '\0';
        }
    } else {
        strncpy(rel, abs, relsz - 1);
        rel[relsz - 1] = '\0';
    }
}

// ── InternalFileSystem methods ────────────────────────────────────────────

bool InternalFileSystem::begin()
{
    if (_mounted)
        return true;

    int rc = fs_mount(&_lfs_mnt);
    if (rc == 0) {
        _mounted = true;
        return true;
    }

    // Mount failed: attempt to format (creates a fresh LittleFS)
    LOG_WARN("LittleFS mount failed (%d), formatting storage partition...", rc);
    int fmt_rc = fs_mkfs(FS_LITTLEFS, (uintptr_t)FIXED_PARTITION_ID(storage_partition), NULL, 0);
    if (fmt_rc != 0) {
        LOG_ERROR("LittleFS format failed (%d)", fmt_rc);
        return false;
    }

    rc = fs_mount(&_lfs_mnt);
    if (rc == 0) {
        _mounted = true;
        return true;
    }

    LOG_ERROR("LittleFS mount failed after format (%d)", rc);
    return false;
}

File InternalFileSystem::open(const char *path, const char *mode)
{
    if (!_mounted)
        return File();

    char abs[NRF54L15_FS_PATHLEN];
    toabs(path, abs, sizeof(abs));

    auto s = std::make_shared<NRF54L15FileState>();
    if (!s)
        return File();

    strncpy(s->fullpath, abs, sizeof(s->fullpath) - 1);
    torel(abs, s->relpath, sizeof(s->relpath));

    // Check whether the path is a directory
    struct fs_dirent entry;
    int stat_rc = fs_stat(abs, &entry);
    if (stat_rc == 0 && entry.type == FS_DIR_ENTRY_DIR) {
        s->is_dir = true;
        if (fs_opendir(&s->dir, abs) == 0) {
            s->valid = true;
            return File(s);
        }
        return File();
    }

    // Open as a regular file
    fs_mode_t flags;
    if (strcmp(mode, FILE_O_WRITE) == 0) {
        // Truncate on write — unlink first to ensure a clean start
        fs_unlink(abs);
        flags = FS_O_WRITE | FS_O_CREATE;
    } else {
        flags = FS_O_READ;
    }

    if (fs_open(&s->file, abs, flags) == 0) {
        s->is_dir = false;
        s->valid = true;
        return File(s);
    }

    return File();
}

bool InternalFileSystem::exists(const char *path)
{
    if (!_mounted)
        return false;
    char abs[NRF54L15_FS_PATHLEN];
    toabs(path, abs, sizeof(abs));
    struct fs_dirent entry;
    return fs_stat(abs, &entry) == 0;
}

bool InternalFileSystem::remove(const char *path)
{
    if (!_mounted)
        return false;
    char abs[NRF54L15_FS_PATHLEN];
    toabs(path, abs, sizeof(abs));
    return fs_unlink(abs) == 0;
}

bool InternalFileSystem::rename(const char *from, const char *to)
{
    if (!_mounted)
        return false;
    char absfrom[NRF54L15_FS_PATHLEN], absto[NRF54L15_FS_PATHLEN];
    toabs(from, absfrom, sizeof(absfrom));
    toabs(to, absto, sizeof(absto));
    return fs_rename(absfrom, absto) == 0;
}

bool InternalFileSystem::mkdir(const char *path)
{
    if (!_mounted)
        return false;
    char abs[NRF54L15_FS_PATHLEN];
    toabs(path, abs, sizeof(abs));
    int rc = fs_mkdir(abs);
    return rc == 0 || rc == -EEXIST;
}

bool InternalFileSystem::rmdir(const char *path)
{
    if (!_mounted)
        return false;
    char abs[NRF54L15_FS_PATHLEN];
    toabs(path, abs, sizeof(abs));
    return fs_unlink(abs) == 0;
}

bool InternalFileSystem::rmdir_r(const char *path)
{
    if (!_mounted)
        return false;
    char abs[NRF54L15_FS_PATHLEN];
    toabs(path, abs, sizeof(abs));

    struct fs_dir_t dir;
    fs_dir_t_init(&dir);
    if (fs_opendir(&dir, abs) != 0) {
        // Not a directory — try to delete as file
        return fs_unlink(abs) == 0;
    }

    struct fs_dirent entry;
    char child[NRF54L15_FS_PATHLEN];
    while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != '\0') {
        snprintf(child, sizeof(child), "%s/%s", abs, entry.name);
        if (entry.type == FS_DIR_ENTRY_DIR) {
            // Recurse: pass the absolute path stripped of mount prefix
            char childrel[NRF54L15_FS_PATHLEN];
            torel(child, childrel, sizeof(childrel));
            rmdir_r(childrel);
        } else {
            fs_unlink(child);
        }
    }
    fs_closedir(&dir);
    return fs_unlink(abs) == 0;
}

bool InternalFileSystem::format()
{
    if (_mounted) {
        fs_unmount(&_lfs_mnt);
        _mounted = false;
    }
    int rc = fs_mkfs(FS_LITTLEFS, (uintptr_t)FIXED_PARTITION_ID(storage_partition), NULL, 0);
    if (rc != 0) {
        LOG_ERROR("LittleFS format failed (%d)", rc);
        return false;
    }
    return begin();
}

// ── File::openNextFile ────────────────────────────────────────────────────
// Defined here because it accesses Zephyr fs_readdir/fs_open APIs.

File File::openNextFile()
{
    if (!_s || !_s->valid || !_s->is_dir)
        return File();

    struct fs_dirent entry;
    if (fs_readdir(&_s->dir, &entry) != 0)
        return File();
    if (entry.name[0] == '\0')
        return File(); // end of directory

    char childabs[NRF54L15_FS_PATHLEN];
    snprintf(childabs, sizeof(childabs), "%s/%s", _s->fullpath, entry.name);

    auto s = std::make_shared<NRF54L15FileState>();
    if (!s)
        return File();

    strncpy(s->fullpath, childabs, sizeof(s->fullpath) - 1);
    torel(childabs, s->relpath, sizeof(s->relpath));

    if (entry.type == FS_DIR_ENTRY_DIR) {
        s->is_dir = true;
        if (fs_opendir(&s->dir, childabs) == 0) {
            s->valid = true;
            return File(s);
        }
    } else {
        s->is_dir = false;
        if (fs_open(&s->file, childabs, FS_O_READ) == 0) {
            s->valid = true;
            return File(s);
        }
    }
    return File();
}
