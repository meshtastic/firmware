#include "graphics/GAT562Identity.h"

#if defined(GAT562)

#include "target_specific.h"
#include <cstdio>

namespace graphics
{

const char *getGAT562BleName()
{
    static char name[16];
    uint8_t mac[6] = {};
    getMacAddr(mac);
    snprintf(name, sizeof(name), "GAT562_%02x%02x", mac[4], mac[5]);
    return name;
}

} // namespace graphics

#endif
