#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    T5_KB_MODE_TEXT_LOWER = 0,
    T5_KB_MODE_TEXT_UPPER,
    T5_KB_MODE_SPECIAL,
    T5_KB_MODE_NUMBER,
} T5KeyboardMode;

typedef enum {
    T5_KB_ACTION_NONE = 0,
    T5_KB_ACTION_CHARACTER,
    T5_KB_ACTION_MODE,
    T5_KB_ACTION_SHIFT,
    T5_KB_ACTION_BACKSPACE,
    T5_KB_ACTION_DELETE,
    T5_KB_ACTION_LEFT,
    T5_KB_ACTION_RIGHT,
    T5_KB_ACTION_SPACE,
    T5_KB_ACTION_SUBMIT,
    T5_KB_ACTION_CANCEL,
} T5KeyboardAction;

typedef enum {
    T5_KB_RESULT_IGNORED = 0,
    T5_KB_RESULT_CHANGED,
    T5_KB_RESULT_MODE_CHANGED,
    T5_KB_RESULT_SUBMIT,
    T5_KB_RESULT_CANCEL,
} T5KeyboardResult;

enum {
    T5_KB_KEY_LETTER_BASE = 0x0100,
    T5_KB_KEY_DIGIT_BASE = 0x0120,
    T5_KB_KEY_SPECIAL_BASE = 0x0140,
    T5_KB_KEY_BACKSPACE = 0x0200,
    T5_KB_KEY_DELETE = 0x0201,
    T5_KB_KEY_LEFT = 0x0202,
    T5_KB_KEY_RIGHT = 0x0203,
    T5_KB_KEY_SPACE = 0x0204,
    T5_KB_KEY_SHIFT = 0x0205,
    T5_KB_KEY_MODE_NUMBER = 0x0206,
    T5_KB_KEY_MODE_SPECIAL = 0x0207,
    T5_KB_KEY_MODE_TEXT = 0x0208,
    T5_KB_KEY_SUBMIT = 0x0209,
    T5_KB_KEY_CANCEL = 0x020A,
};

#define T5_KB_ACTION_QUEUE_CAPACITY 32

typedef struct {
    uint16_t id;
    uint8_t action;
    uint8_t row;
    uint8_t width_weight;
    char character;
    const char *label;
} T5KeyboardKey;

typedef struct {
    char *text;
    uint16_t capacity;
    uint16_t length;
    uint16_t cursor;
    uint16_t max_length;
    const char *accepted_chars;
    uint8_t mode;
    uint8_t one_line;
    uint16_t pressed_key;
} T5KeyboardState;

typedef struct {
    uint16_t key_ids[T5_KB_ACTION_QUEUE_CAPACITY];
    uint8_t head;
    uint8_t count;
} T5KeyboardActionQueue;

void t5_kb_init(T5KeyboardState *keyboard, char *text, uint16_t capacity, uint16_t max_length,
                const char *accepted_chars, uint8_t one_line);
bool t5_kb_set_text(T5KeyboardState *keyboard, const char *text);
void t5_kb_set_mode(T5KeyboardState *keyboard, T5KeyboardMode mode);
T5KeyboardMode t5_kb_mode(const T5KeyboardState *keyboard);
T5KeyboardResult t5_kb_press_key(T5KeyboardState *keyboard, uint16_t key_id);

uint16_t t5_kb_get_key_count(T5KeyboardMode mode);
bool t5_kb_get_key(T5KeyboardMode mode, uint16_t index, T5KeyboardKey *key);

const char *t5_kb_text(const T5KeyboardState *keyboard);
uint16_t t5_kb_length(const T5KeyboardState *keyboard);
uint16_t t5_kb_cursor(const T5KeyboardState *keyboard);
uint16_t t5_kb_pressed_key(const T5KeyboardState *keyboard);
void t5_kb_clear_pressed(T5KeyboardState *keyboard);

void t5_kb_action_queue_init(T5KeyboardActionQueue *queue);
bool t5_kb_action_queue_push(T5KeyboardActionQueue *queue, uint16_t key_id);
bool t5_kb_action_queue_pop(T5KeyboardActionQueue *queue, uint16_t *key_id);
uint8_t t5_kb_action_queue_size(const T5KeyboardActionQueue *queue);

#ifdef __cplusplus
}
#endif
