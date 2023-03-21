#ifndef LITTLEFS_FILE_H_
#define LITTLEFS_FILE_H_

// Forward declaration
class LittleFS;

namespace LittleFS_Namespace
{

// avoid conflict with other FileSystem FILE_READ/FILE_WRITE
enum {
    FILE_O_READ = 0,
    FILE_O_WRITE = 1,
};

class File : public Stream
{
  public:
    explicit File(LittleFS &fs);
    File(char const *filename, uint8_t mode, LittleFS &fs);

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
    LittleFS *_fs;

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

} // namespace LittleFS_Namespace

#endif /* LITTLEFS_FILE_H_ */
