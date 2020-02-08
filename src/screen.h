#pragma once

void screen_print(const char * text);

/// @return how many msecs can we sleep before we want service again
uint32_t screen_loop();

void screen_setup(), screen_on(), screen_off(), screen_press();