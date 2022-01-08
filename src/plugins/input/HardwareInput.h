#pragma once

#define INPUT_EVENT_UP   17
#define INPUT_EVENT_DOWN 18
#define INPUT_EVENT_LEFT 19
#define INPUT_EVENT_RIGHT 20
#define INPUT_EVENT_SELECT '\n'
#define INPUT_EVENT_BACK 27
#define INPUT_EVENT_CANCEL 24

typedef struct _InputEvent {
    char inputEvent;
} InputEvent;