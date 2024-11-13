#include "mesh/http/ContentHelper.h"
// #include <Arduino.h>
// #include "main.h"

void replaceAll(String &str, const String &from, const String &to)
{
    int start_pos = 0;
    while ((start_pos = str.indexOf(from, start_pos)) != -1) {
        str.replace(from, to);
        start_pos += to.length();
    }
}
