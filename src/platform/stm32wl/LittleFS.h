
#ifndef LITTLEFS_H_
#define LITTLEFS_H_

#include <Stream.h>

#include "lfs.h"

#include "LittleFS_File.h"

#include "FreeRTOS.h" // tied to FreeRTOS for serialization
#include "semphr.h"

class LittleFS
{
  public:
    LittleFS(void);
    explicit LittleFS(struct lfs_config *cfg);
    virtual ~LittleFS();

    bool begin(struct lfs_config *cfg = NULL);
    void end(void);

    // Open the specified file/directory with the supplied mode (e.g. read or
    // write, etc). Returns a File object for interacting with the file.
    // Note that currently only one file can be open at a time.
    LittleFS_Namespace::File open(char const *filename, uint8_t mode = LittleFS_Namespace::FILE_O_READ);

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
    void _lockFS(void) { xSemaphoreTake(_mutex, portMAX_DELAY); }
    void _unlockFS(void) { xSemaphoreGive(_mutex); }

  protected:
    bool _mounted;
    struct lfs_config *_lfs_cfg;
    lfs_t _lfs;
    SemaphoreHandle_t _mutex;

  private:
    StaticSemaphore_t _MutexStorageSpace;
};

#if !CFG_DEBUG
#define VERIFY_LFS(...) _GET_3RD_ARG(__VA_ARGS__, VERIFY_ERR_2ARGS, VERIFY_ERR_1ARGS)(__VA_ARGS__, NULL)
#define PRINT_LFS_ERR(_err)
#else
#define VERIFY_LFS(...) _GET_3RD_ARG(__VA_ARGS__, VERIFY_ERR_2ARGS, VERIFY_ERR_1ARGS)(__VA_ARGS__, dbg_strerr_lfs)
#define PRINT_LFS_ERR(_err)                                                                                                      \
    do {                                                                                                                         \
        if (_err) {                                                                                                              \
            VERIFY_MESS((long int)_err, dbg_strerr_lfs);                                                                         \
        }                                                                                                                        \
    } while (0) // LFS_ERR are of type int, VERIFY_MESS expects long_int

const char *dbg_strerr_lfs(int32_t err);
#endif

#endif /* LITTLEFS_H_ */
