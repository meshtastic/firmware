// The Crypto library's RNG reads the Arduino clock, which the framework provides in the pio build.
#include <stdint.h>

extern "C" unsigned long millis()
{
    return 0;
}

extern "C" unsigned long micros()
{
    return 0;
}
