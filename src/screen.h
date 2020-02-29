#pragma once

#include "PeriodicTask.h"

void screen_print(const char * text);
void screen_print(const char * text, uint8_t x, uint8_t y, uint8_t alignment);


// Show the bluetooth PIN screen
void screen_start_bluetooth(uint32_t pin); 

// restore our regular frame list
void screen_set_frames();


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

    /// Is the screen currently on
    bool isOn();

    /// Turn the screen on/off
    void setOn(bool on);

    /// Handle a button press
    void onPress();
};

extern Screen screen;
