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

#ifndef STM32_LITTLEFS_FILE_H_
#define STM32_LITTLEFS_FILE_H_

#include "littlefs/lfs.h"

// Forward declaration
class STM32_LittleFS;

namespace STM32_LittleFS_Namespace
{

// avoid conflict with other FileSystem FILE_READ/FILE_WRITE
enum {
    FILE_O_READ = 0,
    FILE_O_WRITE = 1,
};

class File : public Stream
{
  public:
    explicit File(STM32_LittleFS &fs);
    File(char const *filename, uint8_t mode, STM32_LittleFS &fs);

  public:
    bool open(char const *filename, uint8_t mode);

    //------------- Stream API -------------//
    virtual size_t write(uint8_t ch);
    virtual size_t write(uint8_t const *buf, size_t size);
    size_t write(const char *str)
    {
        if (str == NULL)
            return 0;
        return write((const uint8_t *)str, strlen(str));
    }
    size_t write(const char *buffer, size_t size) { return write((const uint8_t *)buffer, size); }

    virtual int read(void);
    int read(void *buf, uint16_t nbyte);

    virtual int peek(void);
    virtual int available(void);
    virtual void flush(void);

    bool seek(uint32_t pos);
    uint32_t position(void);
    uint32_t size(void);

    bool truncate(uint32_t pos);
    bool truncate(void);

    void close(void);

    operator bool(void);

    bool isOpen(void);
    char const *name(void);

    bool isDirectory(void);
    File openNextFile(uint8_t mode = FILE_O_READ);
    void rewindDirectory(void);

  private:
    STM32_LittleFS *_fs;

    bool _is_dir;

    union {
        lfs_file_t *_file;
        lfs_dir_t *_dir;
    };

    char *_dir_path;
    char _name[LFS_NAME_MAX + 1];

    bool _open(char const *filepath, uint8_t mode);
    bool _open_file(char const *filepath, uint8_t mode);
    bool _open_dir(char const *filepath);
    void _close(void);
};

} // namespace STM32_LittleFS_Namespace

#endif /* STM32_LITTLEFS_FILE_H_ */
