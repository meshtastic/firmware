/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach for Adafruit Industries
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "STM32_LittleFS.h"
#include <Arduino.h>
#include <string.h>

#define memclr(buffer, size) memset(buffer, 0, size)
#define varclr(_var) memclr(_var, sizeof(*(_var)))

using namespace STM32_LittleFS_Namespace;

//--------------------------------------------------------------------+
// Implementation
//--------------------------------------------------------------------+

STM32_LittleFS::STM32_LittleFS(void) : STM32_LittleFS(NULL) {}

STM32_LittleFS::STM32_LittleFS(struct lfs_config *cfg)
{
    varclr(&_lfs);
    _lfs_cfg = cfg;
    _mounted = false;
}

STM32_LittleFS::~STM32_LittleFS() {}

// Initialize and mount the file system
// Return true if mounted successfully else probably corrupted.
// User should format the disk and try again
bool STM32_LittleFS::begin(struct lfs_config *cfg)
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
void STM32_LittleFS::end(void)
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

bool STM32_LittleFS::format(void)
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
STM32_LittleFS_Namespace::File STM32_LittleFS::open(char const *filepath, uint8_t mode)
{
    // No lock is required here ... the File() object will synchronize with the mutex provided
    return STM32_LittleFS_Namespace::File(filepath, mode, *this);
}

// Check if file or folder exists
bool STM32_LittleFS::exists(char const *filepath)
{
    struct lfs_info info;
    _lockFS();

    bool ret = (0 == lfs_stat(&_lfs, filepath, &info));

    _unlockFS();
    return ret;
}

// Create a directory, create intermediate parent if needed
bool STM32_LittleFS::mkdir(char const *filepath)
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
bool STM32_LittleFS::remove(char const *filepath)
{
    _lockFS();

    int err = lfs_remove(&_lfs, filepath);
    PRINT_LFS_ERR(err);

    _unlockFS();
    return LFS_ERR_OK == err;
}

// Rename a file
bool STM32_LittleFS::rename(char const *oldfilepath, char const *newfilepath)
{
    _lockFS();

    int err = lfs_rename(&_lfs, oldfilepath, newfilepath);
    PRINT_LFS_ERR(err);

    _unlockFS();
    return LFS_ERR_OK == err;
}

// Remove a folder
bool STM32_LittleFS::rmdir(char const *filepath)
{
    _lockFS();

    int err = lfs_remove(&_lfs, filepath);
    PRINT_LFS_ERR(err);

    _unlockFS();
    return LFS_ERR_OK == err;
}

// Remove a folder recursively
bool STM32_LittleFS::rmdir_r(char const *filepath)
{
    /* lfs is modified to remove non-empty folder,
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
