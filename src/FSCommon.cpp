#include "configuration.h"
#include "FSCommon.h"

void fsInit()
{
#ifdef FSCom
    if (!FSBegin())
    {
        DEBUG_MSG("ERROR filesystem mount Failed\n");
        assert(0); // FIXME - report failure to phone
    }

    DEBUG_MSG("Filesystem files:\n");
    File dir = FSCom.open("/");
    File f = dir.openNextFile();
    while (f) {
        DEBUG_MSG("  %s\n", f.name());
        f.close();
        f = dir.openNextFile();
    }
#endif
}
