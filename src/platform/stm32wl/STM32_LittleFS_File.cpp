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

#define rtos_malloc malloc
#define rtos_free free

//--------------------------------------------------------------------+
// MACRO TYPEDEF CONSTANT ENUM DECLARATION
//--------------------------------------------------------------------+

using namespace STM32_LittleFS_Namespace;

File::File(STM32_LittleFS &fs)
{
    _fs = &fs;
    _is_dir = false;
    _name[0] = 0;
    _name[LFS_NAME_MAX] = 0;
    _dir_path = NULL;

    _dir = NULL;
    _file = NULL;
}

File::File(char const *filename, uint8_t mode, STM32_LittleFS &fs) : File(fs)
{
    // public constructor calls public API open(), which will obtain the mutex
    this->open(filename, mode);
}

bool File::_open_file(char const *filepath, uint8_t mode)
{
    int flags = (mode == FILE_O_READ) ? LFS_O_RDONLY : (mode == FILE_O_WRITE) ? (LFS_O_RDWR | LFS_O_CREAT) : 0;

    if (flags) {
        _file = (lfs_file_t *)rtos_malloc(sizeof(lfs_file_t));
        if (!_file)
            return false;

        int rc = lfs_file_open(_fs->_getFS(), _file, filepath, flags);

        if (rc) {
            // failed to open
            PRINT_LFS_ERR(rc);
            // free memory
            rtos_free(_file);
            _file = NULL;
            return false;
        }

        // move to end of file
        if (mode == FILE_O_WRITE)
            lfs_file_seek(_fs->_getFS(), _file, 0, LFS_SEEK_END);

        _is_dir = false;
    }

    return true;
}

bool File::_open_dir(char const *filepath)
{
    _dir = (lfs_dir_t *)rtos_malloc(sizeof(lfs_dir_t));
    if (!_dir)
        return false;

    int rc = lfs_dir_open(_fs->_getFS(), _dir, filepath);

    if (rc) {
        // failed to open
        PRINT_LFS_ERR(rc);
        // free memory
        rtos_free(_dir);
        _dir = NULL;
        return false;
    }

    _is_dir = true;

    _dir_path = (char *)rtos_malloc(strlen(filepath) + 1);
    strcpy(_dir_path, filepath);

    return true;
}

bool File::open(char const *filepath, uint8_t mode)
{
    bool ret = false;
    _fs->_lockFS();

    ret = this->_open(filepath, mode);

    _fs->_unlockFS();
    return ret;
}

bool File::_open(char const *filepath, uint8_t mode)
{
    bool ret = false;

    // close if currently opened
    if (this->isOpen())
        _close();

    struct lfs_info info;
    int rc = lfs_stat(_fs->_getFS(), filepath, &info);

    if (LFS_ERR_OK == rc) {
        // file existed, open file or directory accordingly
        ret = (info.type == LFS_TYPE_REG) ? _open_file(filepath, mode) : _open_dir(filepath);
    } else if (LFS_ERR_NOENT == rc) {
        // file not existed, only proceed with FILE_O_WRITE mode
        if (mode == FILE_O_WRITE)
            ret = _open_file(filepath, mode);
    } else {
        PRINT_LFS_ERR(rc);
    }

    // save bare file name
    if (ret) {
        char const *splash = strrchr(filepath, '/');
        strncpy(_name, splash ? (splash + 1) : filepath, LFS_NAME_MAX);
    }
    return ret;
}

size_t File::write(uint8_t ch)
{
    return write(&ch, 1);
}

size_t File::write(uint8_t const *buf, size_t size)
{
    lfs_ssize_t wrcount = 0;
    _fs->_lockFS();

    if (!this->_is_dir) {
        wrcount = lfs_file_write(_fs->_getFS(), _file, buf, size);
        if (wrcount < 0) {
            wrcount = 0;
        }
    }

    _fs->_unlockFS();
    return wrcount;
}

int File::read(void)
{
    // this thin wrapper relies on called function to synchronize
    int ret = -1;
    uint8_t ch;
    if (read(&ch, 1) > 0) {
        ret = static_cast<int>(ch);
    }
    return ret;
}

int File::read(void *buf, uint16_t nbyte)
{
    int ret = 0;
    _fs->_lockFS();

    if (!this->_is_dir) {
        ret = lfs_file_read(_fs->_getFS(), _file, buf, nbyte);
    }

    _fs->_unlockFS();
    return ret;
}

int File::peek(void)
{
    int ret = -1;
    _fs->_lockFS();

    if (!this->_is_dir) {
        uint32_t pos = lfs_file_tell(_fs->_getFS(), _file);
        uint8_t ch = 0;
        if (lfs_file_read(_fs->_getFS(), _file, &ch, 1) > 0) {
            ret = static_cast<int>(ch);
        }
        (void)lfs_file_seek(_fs->_getFS(), _file, pos, LFS_SEEK_SET);
    }

    _fs->_unlockFS();
    return ret;
}

int File::available(void)
{
    int ret = 0;
    _fs->_lockFS();

    if (!this->_is_dir) {
        uint32_t file_size = lfs_file_size(_fs->_getFS(), _file);
        uint32_t pos = lfs_file_tell(_fs->_getFS(), _file);
        ret = file_size - pos;
    }

    _fs->_unlockFS();
    return ret;
}

bool File::seek(uint32_t pos)
{
    bool ret = false;
    _fs->_lockFS();

    if (!this->_is_dir) {
        ret = lfs_file_seek(_fs->_getFS(), _file, pos, LFS_SEEK_SET) >= 0;
    }

    _fs->_unlockFS();
    return ret;
}

uint32_t File::position(void)
{
    uint32_t ret = 0;
    _fs->_lockFS();

    if (!this->_is_dir) {
        ret = lfs_file_tell(_fs->_getFS(), _file);
    }

    _fs->_unlockFS();
    return ret;
}

uint32_t File::size(void)
{
    uint32_t ret = 0;
    _fs->_lockFS();

    if (!this->_is_dir) {
        ret = lfs_file_size(_fs->_getFS(), _file);
    }

    _fs->_unlockFS();
    return ret;
}

bool File::truncate(uint32_t pos)
{
    int32_t ret = LFS_ERR_ISDIR;
    _fs->_lockFS();
    if (!this->_is_dir) {
        ret = lfs_file_truncate(_fs->_getFS(), _file, pos);
    }
    _fs->_unlockFS();
    return (ret == 0);
}

bool File::truncate(void)
{
    int32_t ret = LFS_ERR_ISDIR;
    _fs->_lockFS();
    if (!this->_is_dir) {
        uint32_t pos = lfs_file_tell(_fs->_getFS(), _file);
        ret = lfs_file_truncate(_fs->_getFS(), _file, pos);
    }
    _fs->_unlockFS();
    return (ret == 0);
}

void File::flush(void)
{
    _fs->_lockFS();

    if (!this->_is_dir) {
        lfs_file_sync(_fs->_getFS(), _file);
    }

    _fs->_unlockFS();
    return;
}

void File::close(void)
{
    _fs->_lockFS();
    this->_close();
    _fs->_unlockFS();
}

void File::_close(void)
{
    if (this->isOpen()) {
        if (this->_is_dir) {
            lfs_dir_close(_fs->_getFS(), _dir);
            rtos_free(_dir);
            _dir = NULL;

            if (this->_dir_path)
                rtos_free(_dir_path);
            _dir_path = NULL;
        } else {
            lfs_file_close(this->_fs->_getFS(), _file);
            rtos_free(_file);
            _file = NULL;
        }
    }
}

File::operator bool(void)
{
    return isOpen();
}

bool File::isOpen(void)
{
    return (_file != NULL) || (_dir != NULL);
}

// WARNING -- although marked as `const`, the values pointed
//            to may change.  For example, if the same File
//            object has `open()` called with a different
//            file or directory name, this same pointer will
//            suddenly (unexpectedly?) have different values.
char const *File::name(void)
{
    return this->_name;
}

bool File::isDirectory(void)
{
    return this->_is_dir;
}

File File::openNextFile(uint8_t mode)
{
    _fs->_lockFS();

    File ret(*_fs);
    if (this->_is_dir) {
        struct lfs_info info;
        int rc;

        // lfs_dir_read returns 0 when reaching end of directory, 1 if found an entry
        // Skip the "." and ".." entries ...
        do {
            rc = lfs_dir_read(_fs->_getFS(), _dir, &info);
        } while (rc == 1 && (!strcmp(".", info.name) || !strcmp("..", info.name)));

        if (rc == 1) {
            // string cat name with current folder
            char filepath[strlen(_dir_path) + 1 + strlen(info.name) + 1]; // potential for significant stack usage
            strcpy(filepath, _dir_path);
            if (!(_dir_path[0] == '/' && _dir_path[1] == 0))
                strcat(filepath, "/"); // only add '/' if cwd is not root
            strcat(filepath, info.name);

            (void)ret._open(filepath, mode); // return value is ignored ... caller is expected to check isOpened()
        } else if (rc < 0) {
            PRINT_LFS_ERR(rc);
        }
    }
    _fs->_unlockFS();
    return ret;
}

void File::rewindDirectory(void)
{
    _fs->_lockFS();
    if (this->_is_dir) {
        lfs_dir_rewind(_fs->_getFS(), _dir);
    }
    _fs->_unlockFS();
}
