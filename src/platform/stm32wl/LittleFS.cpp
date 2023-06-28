#include <Arduino.h>
#include <string.h>

#include "LittleFS.h"

using namespace LittleFS_Namespace;

//--------------------------------------------------------------------+
// Implementation
//--------------------------------------------------------------------+

LittleFS::LittleFS(void) : LittleFS(NULL) {}

LittleFS::LittleFS(struct lfs_config *cfg)
{
    memset(&_lfs, 0, sizeof(_lfs));
    _lfs_cfg = cfg;
    _mounted = false;
    _mutex = xSemaphoreCreateMutexStatic(&this->_MutexStorageSpace);
}

LittleFS::~LittleFS() {}

// Initialize and mount the file system
// Return true if mounted successfully else probably corrupted.
// User should format the disk and try again
bool LittleFS::begin(struct lfs_config *cfg)
{
    _lockFS();

    bool ret;
    // not a loop, just an quick way to short-circuit on error
    do {
        if (_mounted) {
            ret = true;
            break;
        }
        if (cfg) {
            _lfs_cfg = cfg;
        }
        if (nullptr == _lfs_cfg) {
            ret = false;
            break;
        }
        // actually attempt to mount, and log error if one occurs
        int err = lfs_mount(&_lfs, _lfs_cfg);
        PRINT_LFS_ERR(err);
        _mounted = (err == LFS_ERR_OK);
        ret = _mounted;
    } while (0);

    _unlockFS();
    return ret;
}

// Tear down and unmount file system
void LittleFS::end(void)
{
    _lockFS();

    if (_mounted) {
        _mounted = false;
        int err = lfs_unmount(&_lfs);
        PRINT_LFS_ERR(err);
        (void)err;
    }

    _unlockFS();
}

bool LittleFS::format(void)
{
    _lockFS();

    int err = LFS_ERR_OK;
    bool attemptMount = _mounted;
    // not a loop, just an quick way to short-circuit on error
    do {
        // if already mounted: umount first -> format -> remount
        if (_mounted) {
            _mounted = false;
            err = lfs_unmount(&_lfs);
            if (LFS_ERR_OK != err) {
                PRINT_LFS_ERR(err);
                break;
            }
        }
        err = lfs_format(&_lfs, _lfs_cfg);
        if (LFS_ERR_OK != err) {
            PRINT_LFS_ERR(err);
            break;
        }

        if (attemptMount) {
            err = lfs_mount(&_lfs, _lfs_cfg);
            if (LFS_ERR_OK != err) {
                PRINT_LFS_ERR(err);
                break;
            }
            _mounted = true;
        }
        // success!
    } while (0);

    _unlockFS();
    return LFS_ERR_OK == err;
}

// Open a file or folder
LittleFS_Namespace::File LittleFS::open(char const *filepath, uint8_t mode)
{
    // No lock is required here ... the File() object will synchronize with the mutex provided
    return LittleFS_Namespace::File(filepath, mode, *this);
}

// Check if file or folder exists
bool LittleFS::exists(char const *filepath)
{
    struct lfs_info info;
    _lockFS();

    bool ret = (0 == lfs_stat(&_lfs, filepath, &info));

    _unlockFS();
    return ret;
}

// Create a directory, create intermediate parent if needed
bool LittleFS::mkdir(char const *filepath)
{
    bool ret = true;
    const char *slash = filepath;
    if (slash[0] == '/')
        slash++; // skip root '/'

    _lockFS();

    // make intermediate parent directory(ies)
    while (NULL != (slash = strchr(slash, '/'))) {
        char parent[slash - filepath + 1] = {0};
        memcpy(parent, filepath, slash - filepath);

        int rc = lfs_mkdir(&_lfs, parent);
        if (rc != LFS_ERR_OK && rc != LFS_ERR_EXIST) {
            PRINT_LFS_ERR(rc);
            ret = false;
            break;
        }
        slash++;
    }
    // make the final requested directory
    if (ret) {
        int rc = lfs_mkdir(&_lfs, filepath);
        if (rc != LFS_ERR_OK && rc != LFS_ERR_EXIST) {
            PRINT_LFS_ERR(rc);
            ret = false;
        }
    }

    _unlockFS();
    return ret;
}

// Remove a file
bool LittleFS::remove(char const *filepath)
{
    _lockFS();

    int err = lfs_remove(&_lfs, filepath);
    PRINT_LFS_ERR(err);

    _unlockFS();
    return LFS_ERR_OK == err;
}

// Rename a file
bool LittleFS::rename(char const *oldfilepath, char const *newfilepath)
{
    _lockFS();

    int err = lfs_rename(&_lfs, oldfilepath, newfilepath);
    PRINT_LFS_ERR(err);

    _unlockFS();
    return LFS_ERR_OK == err;
}

// Remove a folder
bool LittleFS::rmdir(char const *filepath)
{
    _lockFS();

    int err = lfs_remove(&_lfs, filepath);
    PRINT_LFS_ERR(err);

    _unlockFS();
    return LFS_ERR_OK == err;
}

// Remove a folder recursively
bool LittleFS::rmdir_r(char const *filepath)
{
    /* adafruit: lfs is modified to remove non-empty folder,
     According to below issue, comment these 2 line won't corrupt filesystem
     at least when using LFS v1.  If moving to LFS v2, see tracked issue
     to see if issues (such as the orphans in threaded linked list) are resolved.
     https://github.com/ARMmbed/littlefs/issues/43
     */
    _lockFS();

    int err = lfs_remove(&_lfs, filepath);
    PRINT_LFS_ERR(err);

    _unlockFS();
    return LFS_ERR_OK == err;
}

//------------- Debug -------------//
#if CFG_DEBUG

const char *dbg_strerr_lfs(int32_t err)
{
    switch (err) {
    case LFS_ERR_OK:
        return "LFS_ERR_OK";
    case LFS_ERR_IO:
        return "LFS_ERR_IO";
    case LFS_ERR_CORRUPT:
        return "LFS_ERR_CORRUPT";
    case LFS_ERR_NOENT:
        return "LFS_ERR_NOENT";
    case LFS_ERR_EXIST:
        return "LFS_ERR_EXIST";
    case LFS_ERR_NOTDIR:
        return "LFS_ERR_NOTDIR";
    case LFS_ERR_ISDIR:
        return "LFS_ERR_ISDIR";
    case LFS_ERR_NOTEMPTY:
        return "LFS_ERR_NOTEMPTY";
    case LFS_ERR_BADF:
        return "LFS_ERR_BADF";
    case LFS_ERR_INVAL:
        return "LFS_ERR_INVAL";
    case LFS_ERR_NOSPC:
        return "LFS_ERR_NOSPC";
    case LFS_ERR_NOMEM:
        return "LFS_ERR_NOMEM";

    default:
        static char errcode[10];
        sprintf(errcode, "%ld", err);
        return errcode;
    }

    return NULL;
}

#endif
