#include "configuration.h"
#include "FSCommon.h"

void listDir(const char * dirname, uint8_t levels)
#ifdef FSCom
{
    File root = FSCom.open(dirname);
    if(!root){
        return;
    }
    if(!root.isDirectory()){
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            if(levels){
                listDir(file.name(), levels -1);
            }
        } else {
            DEBUG_MSG("  %s (%i Bytes)\n", file.name(), file.size());
        }
        file.close();
        file = root.openNextFile();
    }
    file.close();
#endif
}

void fsInit()
{
#ifdef FSCom
    if (!FSBegin())
    {
        DEBUG_MSG("ERROR filesystem mount Failed. Formatting...\n");
        assert(0); // FIXME - report failure to phone
    }

    DEBUG_MSG("Filesystem files:\n");
    listDir("/", 10);
#endif
}
