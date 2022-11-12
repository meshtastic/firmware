#include "configuration.h"
#include "FSCommon.h"

#ifdef HAS_SDCARD
#include <SPI.h>
#include <SD.h>


#ifdef SDCARD_USE_SPI1  
SPIClass SPI1(HSPI);
#define SDHandler SPI1
#endif


#endif  //HAS_SDCARD

bool copyFile(const char* from, const char* to)
{
#ifdef FSCom
    unsigned char cbuffer[16];
   
    File f1 = FSCom.open(from, FILE_O_READ);
    if (!f1){
        DEBUG_MSG("Failed to open source file %s\n", from);
        return false;
    }

    File f2 = FSCom.open(to, FILE_O_WRITE);
    if (!f2) {
        DEBUG_MSG("Failed to open destination file %s\n", to);
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
#ifdef ARCH_ESP32
    // rename was fixed for ESP32 IDF LittleFS in April
    return FSCom.rename(pathFrom, pathTo);
#else
    if (copyFile(pathFrom, pathTo) && FSCom.remove(pathFrom) ) {
        return true;
    } else{
        return false;
    }
#endif
#endif
}

void listDir(const char * dirname, uint8_t levels, boolean del = false)
{
#ifdef FSCom
#if (defined(ARCH_ESP32) || defined(ARCH_RP2040) || defined(ARCH_PORTDUINO))
    char buffer[255];
#endif
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
#ifdef ARCH_ESP32
                listDir(file.path(), levels -1, del);
                if(del) { 
                    DEBUG_MSG("Removing %s\n", file.path());
                    strcpy(buffer, file.path());
                    file.close();
                    FSCom.rmdir(buffer);
                } else {
                    file.close();
                }
#elif (defined(ARCH_RP2040) || defined(ARCH_PORTDUINO))
                listDir(file.name(), levels -1, del);
                if(del) { 
                    DEBUG_MSG("Removing %s\n", file.name());
                    strcpy(buffer, file.name());
                    file.close();
                    FSCom.rmdir(buffer);
                } else {
                    file.close();
                }                
#else
                listDir(file.name(), levels -1, del);
                file.close();
#endif
            }
        } else {
#ifdef ARCH_ESP32
            if(del) {
                DEBUG_MSG("Deleting %s\n", file.path());
                strcpy(buffer, file.path());
                file.close();
                FSCom.remove(buffer);
            } else {
            DEBUG_MSG(" %s (%i Bytes)\n", file.path(), file.size());
                file.close();
            }
#elif (defined(ARCH_RP2040) || defined(ARCH_PORTDUINO))
            if(del) {
                DEBUG_MSG("Deleting %s\n", file.name());
                strcpy(buffer, file.name());
                file.close();
                FSCom.remove(buffer);
            } else {
                DEBUG_MSG(" %s (%i Bytes)\n", file.name(), file.size());
                file.close();
            }
#else
            DEBUG_MSG(" %s (%i Bytes)\n", file.name(), file.size());
            file.close();
#endif            
        }
        file = root.openNextFile();
    }
#ifdef ARCH_ESP32    
    if(del) { 
        DEBUG_MSG("Removing %s\n", root.path());
        strcpy(buffer, root.path());
        root.close();
        FSCom.rmdir(buffer);
    } else {
        root.close();
    }
#elif (defined(ARCH_RP2040) || defined(ARCH_PORTDUINO))
    if(del) { 
        DEBUG_MSG("Removing %s\n", root.name());
        strcpy(buffer, root.name());
        root.close();
        FSCom.rmdir(buffer);
    } else {
        root.close();
    }
#else
    root.close();
#endif
#endif
}

void rmDir(const char * dirname)
{
#ifdef FSCom
#if (defined(ARCH_ESP32) || defined(ARCH_RP2040) || defined(ARCH_PORTDUINO))
    listDir(dirname, 10, true);
#elif defined(ARCH_NRF52)
    // nRF52 implementation of LittleFS has a recursive delete function
    FSCom.rmdir_r(dirname);
#endif
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
#ifdef ARCH_ESP32
    DEBUG_MSG("Filesystem files (%d/%d Bytes):\n", FSCom.usedBytes(), FSCom.totalBytes());
#else
    DEBUG_MSG("Filesystem files:\n");
#endif
    listDir("/", 10);
#endif
}


void setupSDCard()
{
#ifdef HAS_SDCARD
    SDHandler.begin(SPI_SCK, SPI_MISO, SPI_MOSI);

    if (!SD.begin(SDCARD_CS, SDHandler)) {
        DEBUG_MSG("No SD_MMC card detected\n");
        return ;
    }
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        DEBUG_MSG("No SD_MMC card attached\n");
        return ;
    }
    DEBUG_MSG("SD_MMC Card Type: ");
    if (cardType == CARD_MMC) {
        DEBUG_MSG("MMC\n");
    } else if (cardType == CARD_SD) {
        DEBUG_MSG("SDSC\n");
    } else if (cardType == CARD_SDHC) {
        DEBUG_MSG("SDHC\n");
    } else {
        DEBUG_MSG("UNKNOWN\n");
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    DEBUG_MSG("SD Card Size: %lluMB\n", cardSize);
    DEBUG_MSG("Total space: %llu MB\n", SD.totalBytes() / (1024 * 1024));
    DEBUG_MSG("Used space: %llu MB\n", SD.usedBytes() / (1024 * 1024));
#endif
}



