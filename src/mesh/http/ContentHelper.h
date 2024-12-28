#include <Arduino.h>
#include <functional>
#include <string>

#define BoolToString(x) ((x) ? "true" : "false")

void replaceAll(std::string &str, const std::string &from, const std::string &to);
