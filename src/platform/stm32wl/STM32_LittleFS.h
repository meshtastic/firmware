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

#ifndef STM32_LITTLEFS_H_
#define STM32_LITTLEFS_H_

#include <Stream.h>

// Internal Flash uses ARM Little FileSystem
// https://github.com/ARMmbed/littlefs
#include "../../freertosinc.h" // tied to FreeRTOS for serialization
#include "STM32_LittleFS_File.h"
#include "littlefs/lfs.h"

class STM32_LittleFS
{
  public:
    STM32_LittleFS(void);
    explicit STM32_LittleFS(struct lfs_config *cfg);
    virtual ~STM32_LittleFS();

    bool begin(struct lfs_config *cfg = NULL);
    void end(void);

    // Open the specified file/directory with the supplied mode (e.g. read or
    // write, etc). Returns a File object for interacting with the file.
    // Note that currently only one file can be open at a time.
    STM32_LittleFS_Namespace::File open(char const *filename, uint8_t mode = STM32_LittleFS_Namespace::FILE_O_READ);

    // Methods to determine if the requested file path exists.
    bool exists(char const *filepath);

    // Create the requested directory hierarchy--if intermediate directories
    // do not exist they will be created.
    bool mkdir(char const *filepath);

    // Delete the file.
    bool remove(char const *filepath);

    // Rename the file.
    bool rename(char const *oldfilepath, char const *newfilepath);

    // Delete a folder (must be empty)
    bool rmdir(char const *filepath);

    // Delete a folder (recursively)
    bool rmdir_r(char const *filepath);

    // format file system
    bool format(void);

    /*------------------------------------------------------------------*/
    /* INTERNAL USAGE ONLY
     * Although declare as public, it is meant to be invoked by internal
     * code. User should not call these directly
     *------------------------------------------------------------------*/
    lfs_t *_getFS(void) { return &_lfs; }
    void _lockFS(void)
    { /* no-op */
    }
    void _unlockFS(void)
    { /* no-op */
    }

  protected:
    bool _mounted;
    struct lfs_config *_lfs_cfg;
    lfs_t _lfs;
};

#if !CFG_DEBUG
#define VERIFY_LFS(...) _GET_3RD_ARG(__VA_ARGS__, VERIFY_ERR_2ARGS, VERIFY_ERR_1ARGS)(__VA_ARGS__, NULL)
#define PRINT_LFS_ERR(_err)
#else
#define VERIFY_LFS(...) _GET_3RD_ARG(__VA_ARGS__, VERIFY_ERR_2ARGS, VERIFY_ERR_1ARGS)(__VA_ARGS__, dbg_strerr_lfs)
#define PRINT_LFS_ERR(_err)                                                                                                      \
    do {                                                                                                                         \
        if (_err) {                                                                                                              \
            printf("%s:%d, LFS error: %d\n", __FILE__, __LINE__, _err);                                                          \
        }                                                                                                                        \
    } while (0) // LFS_ERR are of type int, VERIFY_MESS expects long_int

const char *dbg_strerr_lfs(int32_t err);
#endif

#endif /* STM32_LITTLEFS_H_ */
