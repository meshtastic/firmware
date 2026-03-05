#pragma once

#include "mapps/FileBackend.h"
#include "FSCommon.h"

#ifdef FSCom

// FileBackend implementation wrapping the on-device flash filesystem (FSCom).
class FlashFileBackend : public FileBackend
{
  public:
    std::vector<DirEntry> listDir(const char *path) override
    {
        std::vector<DirEntry> result;

        File root = FSCom.open(path, FILE_O_READ);
        if (!root || !root.isDirectory())
            return result;

        File entry;
        while ((entry = root.openNextFile())) {
            DirEntry de;
            de.name = entry.name();
            de.isDirectory = entry.isDirectory();
            result.push_back(std::move(de));
            entry.close();
        }
        root.close();

        return result;
    }

    std::string readFile(const char *path) override
    {
        File file = FSCom.open(path, FILE_O_READ);
        if (!file)
            return "";

        size_t size = file.size();
        if (size == 0) {
            file.close();
            return "";
        }

        std::string content;
        content.resize(size);
        file.read((uint8_t *)content.data(), size);
        file.close();
        return content;
    }

    bool writeFile(const char *path, const std::string &data) override
    {
        File file = FSCom.open(path, FILE_O_WRITE);
        if (!file)
            return false;

        file.write((const uint8_t *)data.data(), data.size());
        file.close();
        return true;
    }
};

#endif // FSCom
