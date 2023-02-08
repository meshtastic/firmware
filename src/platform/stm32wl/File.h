// class File
// {
// public:
//     File();
//     File(char const *filename, char const *mode);

// public:
//     bool open(char const *filename, char const *mode);

//     //------------- Stream API -------------//
//     virtual size_t write(uint8_t ch) = 0;
//     virtual size_t write(uint8_t const *buf, size_t size) = 0;
//     size_t write(const char *str)
//     {
//         if (str == NULL)
//             return 0;
//         return write((const uint8_t *)str, strlen(str));
//     }
//     size_t write(const char *buffer, size_t size)
//     {
//         return write((const uint8_t *)buffer, size);
//     }

//     virtual int read(void) = 0;
//     int read(void *buf, uint16_t nbyte);

//     virtual int peek(void) = 0;
//     virtual int available(void) = 0;
//     virtual void flush(void);

//     bool seek(uint32_t pos);
//     uint32_t position(void);
//     uint32_t size(void);

//     bool truncate(uint32_t pos);
//     bool truncate(void);

//     void close(void);

//     operator bool(void);

//     bool isOpen(void);
//     char const *name(void);

//     bool isDirectory(void);
//     File openNextFile(char const *mode = "r");
// };