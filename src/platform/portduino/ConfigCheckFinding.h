#pragma once

#ifndef ARCH_PORTDUINO_WASM

#include <string>

namespace configcheck
{

enum Level { kInfo, kWarn, kError };

struct Finding {
    Level level;
    std::string file;
    int line; // 1-based; 0 when the finding is not tied to a line
    std::string message;
};

} // namespace configcheck

#endif // !ARCH_PORTDUINO_WASM
