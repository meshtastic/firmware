#include "configuration.h"
#include "FSCommon.h"


bool copyFile(const char* from, const char* to)
{
#ifdef FSCom
    unsigned char cbuffer[16];
   
    File f1 = FSCom.open(from, FILE_O_READ);
    if (!f1){
        DEBUG_MSG("Failed to open file");
        return false;
    }

    File f2 = FSCom.open(to, FILE_O_WRITE);
    if (!f2) {
        DEBUG_MSG("Failed to open file");
        return false;
    }
   
    while (f1.available() > 0) {
        byte i = f1.read(cbuffer, 16);
        f2.write(cbuffer, i);
    }
   
    f2.close();
    f1.close();
    return true;
#endif
}

bool renameFile(const char* pathFrom, const char* pathTo)
{
#ifdef FSCom
    if (copyFile(pathFrom, pathTo) && FSCom.remove(pathFrom) ) {
        return true;
    } else{
        return false;
    }
#endif
}

void listDir(const char * dirname, uint8_t levels)
{
#ifdef FSCom
    File root = FSCom.open(dirname, FILE_O_READ);
    if(!root){
        return;
    }
    if(!root.isDirectory()){
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory() && !String(file.name()).endsWith(".")) {
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

void rmDir(const char * dirname)
{
#ifdef FSCom
    File file = FSCom.open(dirname, FILE_O_READ);
    if(!file){
        return;
    }
    if(!file.isDirectory()){
        file.close();
        FSCom.remove(file.name());
      //  DEBUG_MSG("Remove FILE  %s\n", file.name());
        return;
    }

    file.rewindDirectory();
    while (true) {
        File entry = file.openNextFile();
        if (!entry) {
          break;
        }
        char dirpath[100];   // array to hold the result.
        strcpy(dirpath, dirname); // copy string one into the result.
        strcat(dirpath,"/"); // append string two to the result.
        strcat(dirpath,entry.name()); // append string two to the result.
        if(entry.isDirectory() && !String(entry.name()).endsWith(".")) {
            entry.close();
         //   DEBUG_MSG("Descend DIR  %s\n", dirpath);
            rmDir(dirpath);
        } else {
            entry.close();
         //   DEBUG_MSG("Remove FILE  %s\n", entry.name());
            FSCom.remove(entry.name());
        }
    }
    FSCom.rmdir(dirname);
   // DEBUG_MSG("Remove DIR  %s\n", dirname);
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
