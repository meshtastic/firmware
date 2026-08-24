#include "T5S3KeyboardCore.h"

#include <string.h>

static const char lower_labels[26][2] = {
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
    "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z",
};

static const char upper_labels[26][2] = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
};

static const char digit_labels[10][2] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};

static const char text_row0[] = "qwertyuiop";
static const char text_row1[] = "asdfghjkl.?";
static const char text_row2[] = "zxcvbnm,/";
static const char number_row1[] = "-/:;()$&@\"#";
static const char number_row2[] = ".,?!'+=_%*";
static const char special_row0[] = "~`|\\^[]{}*";
static const char special_row1[] = "#@&%$+-=_<>";
static const char special_row2[] = ".,?!'\":;/?";
static char symbol_label[2] = {0, 0};

static uint16_t bounded_max_length(uint16_t capacity, uint16_t max_length)
{
    if (capacity == 0)
        return 0;
    return max_length < capacity - 1 ? max_length : capacity - 1;
}

static bool is_allowed(const T5KeyboardState *keyboard, char character)
{
    if (character < 32 || character > 126)
        return false;
    if (keyboard->accepted_chars == NULL)
        return true;
    return strchr(keyboard->accepted_chars, character) != NULL;
}

static bool insert_character(T5KeyboardState *keyboard, char character)
{
    if (keyboard == NULL || keyboard->text == NULL || keyboard->length >= keyboard->max_length ||
        !is_allowed(keyboard, character))
        return false;

    memmove(keyboard->text + keyboard->cursor + 1, keyboard->text + keyboard->cursor,
            keyboard->length - keyboard->cursor + 1);
    keyboard->text[keyboard->cursor] = character;
    keyboard->length++;
    keyboard->cursor++;
    return true;
}

static bool erase_left(T5KeyboardState *keyboard)
{
    if (keyboard == NULL || keyboard->text == NULL || keyboard->cursor == 0)
        return false;

    memmove(keyboard->text + keyboard->cursor - 1, keyboard->text + keyboard->cursor,
            keyboard->length - keyboard->cursor + 1);
    keyboard->length--;
    keyboard->cursor--;
    return true;
}

static bool erase_right(T5KeyboardState *keyboard)
{
    if (keyboard == NULL || keyboard->text == NULL || keyboard->cursor >= keyboard->length)
        return false;

    memmove(keyboard->text + keyboard->cursor, keyboard->text + keyboard->cursor + 1,
            keyboard->length - keyboard->cursor);
    keyboard->length--;
    return true;
}

static bool move_cursor(T5KeyboardState *keyboard, int16_t delta)
{
    if (keyboard == NULL)
        return false;
    if (delta < 0 && keyboard->cursor > 0) {
        keyboard->cursor--;
        return true;
    }
    if (delta > 0 && keyboard->cursor < keyboard->length) {
        keyboard->cursor++;
        return true;
    }
    return false;
}

static uint16_t add_character_key(T5KeyboardKey *key, uint16_t index, uint8_t row, const char *row_text,
                                  uint8_t width_weight, T5KeyboardMode mode)
{
    const char character = row_text[index];
    key->action = T5_KB_ACTION_CHARACTER;
    key->row = row;
    key->width_weight = width_weight;
    if (character >= 'a' && character <= 'z') {
        key->id = T5_KB_KEY_LETTER_BASE + (uint16_t)(character - 'a');
        key->character = mode == T5_KB_MODE_TEXT_UPPER ? (char)(character - 'a' + 'A') : character;
        key->label = mode == T5_KB_MODE_TEXT_UPPER ? upper_labels[character - 'a'] : lower_labels[character - 'a'];
    } else {
        key->id = T5_KB_KEY_SPECIAL_BASE + (uint16_t)(unsigned char)character;
        key->character = character;
        symbol_label[0] = character;
        key->label = symbol_label;
    }
    return 1;
}

static uint16_t add_literal_key(T5KeyboardKey *key, uint16_t id, uint8_t action, uint8_t row, uint8_t width_weight,
                                char character, const char *label)
{
    key->id = id;
    key->action = action;
    key->row = row;
    key->width_weight = width_weight;
    key->character = character;
    key->label = label;
    return 1;
}

static uint16_t add_digit_key(T5KeyboardKey *key, uint16_t index, uint8_t row, uint8_t width_weight)
{
    const char character = (char)('0' + index);
    key->id = T5_KB_KEY_DIGIT_BASE + index;
    key->action = T5_KB_ACTION_CHARACTER;
    key->row = row;
    key->width_weight = width_weight;
    key->character = character;
    key->label = digit_labels[index];
    return 1;
}

static uint16_t add_symbol_key(T5KeyboardKey *key, uint16_t id_index, uint16_t character_index, uint8_t row,
                               const char *row_text, uint8_t width_weight)
{
    key->id = T5_KB_KEY_SPECIAL_BASE + id_index;
    key->action = T5_KB_ACTION_CHARACTER;
    key->row = row;
    key->width_weight = width_weight;
    key->character = row_text[character_index];
    symbol_label[0] = key->character;
    key->label = symbol_label;
    return 1;
}

uint16_t t5_kb_get_key_count(T5KeyboardMode mode)
{
    switch (mode) {
    case T5_KB_MODE_TEXT_LOWER:
    case T5_KB_MODE_TEXT_UPPER:
        return 38;
    case T5_KB_MODE_SPECIAL:
    case T5_KB_MODE_NUMBER:
        return 38;
    default:
        return 0;
    }
}

bool t5_kb_get_key(T5KeyboardMode mode, uint16_t index, T5KeyboardKey *key)
{
    uint16_t cursor = 0;
    if (key == NULL || index >= t5_kb_get_key_count(mode))
        return false;

    memset(key, 0, sizeof(*key));

    if (mode == T5_KB_MODE_TEXT_LOWER || mode == T5_KB_MODE_TEXT_UPPER) {
        if (index < 10) {
            add_character_key(key, index, 0, text_row0, 1, mode);
            return true;
        }
        cursor = 10;
        if (index == cursor) {
            add_literal_key(key, T5_KB_KEY_BACKSPACE, T5_KB_ACTION_BACKSPACE, 0, 2, 0, "Back");
            return true;
        }
        cursor++;
        if (index < cursor + 11) {
            add_character_key(key, (uint16_t)(index - cursor), 1, text_row1, 1, mode);
            return true;
        }
        cursor += 11;
        if (index == cursor) {
            add_literal_key(key, T5_KB_KEY_SHIFT, T5_KB_ACTION_SHIFT, 2, 2, 0, "Shift");
            return true;
        }
        cursor++;
        if (index < cursor + 9) {
            add_character_key(key, (uint16_t)(index - cursor), 2, text_row2, 1, mode);
            return true;
        }
        cursor += 9;
        if (index == cursor) {
            add_literal_key(key, T5_KB_KEY_DELETE, T5_KB_ACTION_DELETE, 2, 2, 0, "Del");
            return true;
        }
        cursor++;
        if (index == cursor)
            return add_literal_key(key, T5_KB_KEY_MODE_NUMBER, T5_KB_ACTION_MODE, 3, 2, 0, "123") != 0;
        cursor++;
        if (index == cursor)
            return add_literal_key(key, T5_KB_KEY_SPACE, T5_KB_ACTION_SPACE, 3, 5, ' ', "Space") != 0;
        cursor++;
        if (index == cursor)
            return add_literal_key(key, T5_KB_KEY_LEFT, T5_KB_ACTION_LEFT, 3, 2, 0, "<") != 0;
        cursor++;
        if (index == cursor)
            return add_literal_key(key, T5_KB_KEY_RIGHT, T5_KB_ACTION_RIGHT, 3, 2, 0, ">") != 0;
        cursor++;
        return add_literal_key(key, T5_KB_KEY_MODE_SPECIAL, T5_KB_ACTION_MODE, 3, 2, 0, "#+=") != 0;
    }

    if (mode == T5_KB_MODE_NUMBER) {
        if (index < 10) {
            add_digit_key(key, index, 0, 1);
            return true;
        }
        cursor = 10;
        if (index == cursor) {
            add_literal_key(key, T5_KB_KEY_BACKSPACE, T5_KB_ACTION_BACKSPACE, 0, 2, 0, "Back");
            return true;
        }
        cursor++;
        if (index < cursor + 11) {
            add_symbol_key(key, (uint16_t)(10 + index - cursor), (uint16_t)(index - cursor), 1, number_row1, 1);
            return true;
        }
        cursor += 11;
        if (index < cursor + 10) {
            add_symbol_key(key, (uint16_t)(21 + index - cursor), (uint16_t)(index - cursor), 2, number_row2, 1);
            return true;
        }
        cursor += 10;
        if (index == cursor) {
            add_literal_key(key, T5_KB_KEY_DELETE, T5_KB_ACTION_DELETE, 2, 2, 0, "Del");
            return true;
        }
        cursor++;
        if (index == cursor)
            return add_literal_key(key, T5_KB_KEY_MODE_TEXT, T5_KB_ACTION_MODE, 3, 2, 0, "ABC") != 0;
        cursor++;
        if (index == cursor)
            return add_literal_key(key, T5_KB_KEY_SPACE, T5_KB_ACTION_SPACE, 3, 5, ' ', "Space") != 0;
        cursor++;
        if (index == cursor)
            return add_literal_key(key, T5_KB_KEY_LEFT, T5_KB_ACTION_LEFT, 3, 2, 0, "<") != 0;
        cursor++;
        if (index == cursor)
            return add_literal_key(key, T5_KB_KEY_RIGHT, T5_KB_ACTION_RIGHT, 3, 2, 0, ">") != 0;
        cursor++;
        return add_literal_key(key, T5_KB_KEY_MODE_SPECIAL, T5_KB_ACTION_MODE, 3, 2, 0, "#+=") != 0;
    }

    if (index < 10) {
        add_symbol_key(key, index, index, 0, special_row0, 1);
        return true;
    }
    cursor = 10;
    if (index == cursor) {
        add_literal_key(key, T5_KB_KEY_BACKSPACE, T5_KB_ACTION_BACKSPACE, 0, 2, 0, "Back");
        return true;
    }
    cursor++;
    if (index < cursor + 11) {
        add_symbol_key(key, (uint16_t)(10 + index - cursor), (uint16_t)(index - cursor), 1, special_row1, 1);
        return true;
    }
    cursor += 11;
    if (index < cursor + 10) {
        add_symbol_key(key, (uint16_t)(21 + index - cursor), (uint16_t)(index - cursor), 2, special_row2, 1);
        return true;
    }
    cursor += 10;
    if (index == cursor) {
        add_literal_key(key, T5_KB_KEY_DELETE, T5_KB_ACTION_DELETE, 2, 2, 0, "Del");
        return true;
    }
    cursor++;
    if (index == cursor)
        return add_literal_key(key, T5_KB_KEY_MODE_NUMBER, T5_KB_ACTION_MODE, 3, 2, 0, "123") != 0;
    cursor++;
    if (index == cursor)
        return add_literal_key(key, T5_KB_KEY_SPACE, T5_KB_ACTION_SPACE, 3, 5, ' ', "Space") != 0;
    cursor++;
    if (index == cursor)
        return add_literal_key(key, T5_KB_KEY_LEFT, T5_KB_ACTION_LEFT, 3, 2, 0, "<") != 0;
    cursor++;
    if (index == cursor)
        return add_literal_key(key, T5_KB_KEY_RIGHT, T5_KB_ACTION_RIGHT, 3, 2, 0, ">") != 0;
    cursor++;
    return add_literal_key(key, T5_KB_KEY_MODE_TEXT, T5_KB_ACTION_MODE, 3, 2, 0, "ABC") != 0;
}

void t5_kb_init(T5KeyboardState *keyboard, char *text, uint16_t capacity, uint16_t max_length,
                const char *accepted_chars, uint8_t one_line)
{
    if (keyboard == NULL)
        return;

    keyboard->text = text;
    keyboard->capacity = capacity;
    keyboard->length = 0;
    keyboard->cursor = 0;
    keyboard->max_length = bounded_max_length(capacity, max_length);
    keyboard->accepted_chars = accepted_chars;
    keyboard->mode = T5_KB_MODE_TEXT_LOWER;
    keyboard->one_line = one_line;
    keyboard->pressed_key = 0;
    if (text != NULL && capacity > 0)
        text[0] = '\0';
}

bool t5_kb_set_text(T5KeyboardState *keyboard, const char *text)
{
    uint16_t length = 0;
    if (keyboard == NULL || keyboard->text == NULL || keyboard->capacity == 0)
        return false;
    if (text == NULL)
        text = "";

    while (text[length] != '\0' && length < keyboard->max_length) {
        if (is_allowed(keyboard, text[length])) {
            keyboard->text[keyboard->length++] = text[length];
        }
        length++;
    }
    keyboard->text[keyboard->length] = '\0';
    keyboard->cursor = keyboard->length;
    return true;
}

void t5_kb_set_mode(T5KeyboardState *keyboard, T5KeyboardMode mode)
{
    if (keyboard == NULL)
        return;
    if (mode <= T5_KB_MODE_NUMBER)
        keyboard->mode = (uint8_t)mode;
}

T5KeyboardMode t5_kb_mode(const T5KeyboardState *keyboard)
{
    return keyboard == NULL ? T5_KB_MODE_TEXT_LOWER : (T5KeyboardMode)keyboard->mode;
}

static bool find_key(T5KeyboardMode mode, uint16_t key_id, T5KeyboardKey *key)
{
    if (key_id == T5_KB_KEY_SUBMIT) {
        add_literal_key(key, T5_KB_KEY_SUBMIT, T5_KB_ACTION_SUBMIT, 4, 3, 0, "Send");
        return true;
    }
    if (key_id == T5_KB_KEY_CANCEL) {
        add_literal_key(key, T5_KB_KEY_CANCEL, T5_KB_ACTION_CANCEL, 4, 3, 0, "Cancel");
        return true;
    }

    const uint16_t count = t5_kb_get_key_count(mode);
    for (uint16_t index = 0; index < count; index++) {
        if (!t5_kb_get_key(mode, index, key))
            continue;
        if (key->id == key_id)
            return true;
    }
    return false;
}

T5KeyboardResult t5_kb_press_key(T5KeyboardState *keyboard, uint16_t key_id)
{
    T5KeyboardKey key;
    bool changed = false;
    if (keyboard == NULL || !find_key(t5_kb_mode(keyboard), key_id, &key))
        return T5_KB_RESULT_IGNORED;

    keyboard->pressed_key = key_id;
    switch ((T5KeyboardAction)key.action) {
    case T5_KB_ACTION_CHARACTER:
        changed = insert_character(keyboard, key.character);
        if (changed && keyboard->mode == T5_KB_MODE_TEXT_UPPER)
            keyboard->mode = T5_KB_MODE_TEXT_LOWER;
        return changed ? T5_KB_RESULT_CHANGED : T5_KB_RESULT_IGNORED;
    case T5_KB_ACTION_SPACE:
        return insert_character(keyboard, ' ') ? T5_KB_RESULT_CHANGED : T5_KB_RESULT_IGNORED;
    case T5_KB_ACTION_BACKSPACE:
        return erase_left(keyboard) ? T5_KB_RESULT_CHANGED : T5_KB_RESULT_IGNORED;
    case T5_KB_ACTION_DELETE:
        return erase_right(keyboard) ? T5_KB_RESULT_CHANGED : T5_KB_RESULT_IGNORED;
    case T5_KB_ACTION_LEFT:
        return move_cursor(keyboard, -1) ? T5_KB_RESULT_CHANGED : T5_KB_RESULT_IGNORED;
    case T5_KB_ACTION_RIGHT:
        return move_cursor(keyboard, 1) ? T5_KB_RESULT_CHANGED : T5_KB_RESULT_IGNORED;
    case T5_KB_ACTION_SHIFT:
        if (keyboard->mode == T5_KB_MODE_TEXT_LOWER) {
            keyboard->mode = T5_KB_MODE_TEXT_UPPER;
            return T5_KB_RESULT_MODE_CHANGED;
        }
        if (keyboard->mode == T5_KB_MODE_TEXT_UPPER) {
            keyboard->mode = T5_KB_MODE_TEXT_LOWER;
            return T5_KB_RESULT_MODE_CHANGED;
        }
        return T5_KB_RESULT_IGNORED;
    case T5_KB_ACTION_MODE:
        if (key.id == T5_KB_KEY_MODE_NUMBER)
            keyboard->mode = T5_KB_MODE_NUMBER;
        else if (key.id == T5_KB_KEY_MODE_SPECIAL)
            keyboard->mode = T5_KB_MODE_SPECIAL;
        else if (key.id == T5_KB_KEY_MODE_TEXT)
            keyboard->mode = T5_KB_MODE_TEXT_LOWER;
        else
            return T5_KB_RESULT_IGNORED;
        return T5_KB_RESULT_MODE_CHANGED;
    case T5_KB_ACTION_SUBMIT:
        return keyboard->length > 0 ? T5_KB_RESULT_SUBMIT : T5_KB_RESULT_IGNORED;
    case T5_KB_ACTION_CANCEL:
        return T5_KB_RESULT_CANCEL;
    default:
        return T5_KB_RESULT_IGNORED;
    }
}

const char *t5_kb_text(const T5KeyboardState *keyboard)
{
    return keyboard == NULL || keyboard->text == NULL ? "" : keyboard->text;
}

uint16_t t5_kb_length(const T5KeyboardState *keyboard)
{
    return keyboard == NULL ? 0 : keyboard->length;
}

uint16_t t5_kb_cursor(const T5KeyboardState *keyboard)
{
    return keyboard == NULL ? 0 : keyboard->cursor;
}

uint16_t t5_kb_pressed_key(const T5KeyboardState *keyboard)
{
    return keyboard == NULL ? 0 : keyboard->pressed_key;
}

void t5_kb_clear_pressed(T5KeyboardState *keyboard)
{
    if (keyboard != NULL)
        keyboard->pressed_key = 0;
}

void t5_kb_action_queue_init(T5KeyboardActionQueue *queue)
{
    if (queue == NULL)
        return;
    memset(queue, 0, sizeof(*queue));
}

bool t5_kb_action_queue_push(T5KeyboardActionQueue *queue, uint16_t key_id)
{
    if (queue == NULL || queue->count >= T5_KB_ACTION_QUEUE_CAPACITY)
        return false;

    const uint8_t index = (uint8_t)((queue->head + queue->count) % T5_KB_ACTION_QUEUE_CAPACITY);
    queue->key_ids[index] = key_id;
    queue->count++;
    return true;
}

bool t5_kb_action_queue_pop(T5KeyboardActionQueue *queue, uint16_t *key_id)
{
    if (queue == NULL || key_id == NULL || queue->count == 0)
        return false;

    *key_id = queue->key_ids[queue->head];
    queue->head = (uint8_t)((queue->head + 1) % T5_KB_ACTION_QUEUE_CAPACITY);
    queue->count--;
    return true;
}

uint8_t t5_kb_action_queue_size(const T5KeyboardActionQueue *queue)
{
    return queue == NULL ? 0 : queue->count;
}
