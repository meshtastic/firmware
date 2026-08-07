#include "mesh/cell/cellModem.h"

#if HAS_CELLULAR

#include "mesh/cell/ATModem.h"

#if defined(USE_EC618)
#include "mesh/cell/EC618Modem.h"
#else
#error "HAS_CELLULAR is set, but no modem driver (USE_EC618) is selected."
#endif

void initCellModem()
{
    if (cellModem)
        return;

#if defined(USE_EC618)
    cellModem = new EC618Modem();
#endif

    cellModem->start();
}

#endif
