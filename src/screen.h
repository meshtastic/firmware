#pragma once

#include "PeriodicTask.h"

void screen_print(const char * text);

void screen_on(), screen_off(), screen_press();

// Show the bluetooth PIN screen
void screen_start_bluetooth(uint32_t pin); 

// restore our regular frame list
void screen_set_frames();

bool is_screen_on();

/**
 * Slowly I'm moving screen crap into this class
 */
class Screen : public PeriodicTask
{
public:

    void setup();

    virtual void doTask();

    /// Turn on the screen asap
    void doWakeScreen();
};

extern Screen screen;