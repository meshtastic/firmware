#include "kbLayout.h"
// #include "configuration.h"
// #include <ctype.h>
// #include <string.h>

// Keyboard layout mapping type
typedef const char *(*MapKeyFn)(char);

typedef struct {
    const char *name;
    MapKeyFn mapKey;
} KeyboardLayout;

static const char *mapEnglish(char key)
{
    static char singleChar[2] = {0};
    singleChar[0] = key;
    singleChar[1] = 0;
    return singleChar;
}

#ifdef OLED_RU
static const char *mapRussian(char key)
{
    switch (key) {
    case 'a':
        return "ф";
    case 'b':
        return "и";
    case 'c':
        return "с";
    case 'd':
        return "в";
    case 'e':
        return "у";
    case 'f':
        return "а";
    case 'g':
        return "п";
    case 'h':
        return "р";
    case 'i':
        return "ш";
    case 'j':
        return "о";
    case 'k':
        return "л";
    case 'l':
        return "д";
    case 'm':
        return "ь";
    case 'n':
        return "т";
    case 'o':
        return "щ";
    case 'p':
        return "з";
    case 'q':
        return "й";
    case 'r':
        return "к";
    case 's':
        return "ы";
    case 't':
        return "е";
    case 'u':
        return "г";
    case 'v':
        return "м";
    case 'w':
        return "ц";
    case 'x':
        return "ч";
    case 'y':
        return "н";
    case 'z':
        return "я";
    case 'A':
        return "Ф";
    case 'B':
        return "И";
    case 'C':
        return "С";
    case 'D':
        return "В";
    case 'E':
        return "У";
    case 'F':
        return "А";
    case 'G':
        return "П";
    case 'H':
        return "Р";
    case 'I':
        return "Ш";
    case 'J':
        return "О";
    case 'K':
        return "Л";
    case 'L':
        return "Д";
    case 'M':
        return "Ь";
    case 'N':
        return "Т";
    case 'O':
        return "Щ";
    case 'P':
        return "З";
    case 'Q':
        return "Й";
    case 'R':
        return "К";
    case 'S':
        return "Ы";
    case 'T':
        return "Е";
    case 'U':
        return "Г";
    case 'V':
        return "М";
    case 'W':
        return "Ц";
    case 'X':
        return "Ч";
    case 'Y':
        return "Н";
    case 'Z':
        return "Я";
    case '[':
        return "х";
    case ']':
        return "ъ";
    case ';':
        return "ж";
    case '\'':
        return "э";
    case ',':
        return "б";
    case '.':
        return "ю";
    case '`':
        return "ё";
    case '{':
        return "Х";
    case '}':
        return "Ъ";
    case ':':
        return "Ж";
    case '"':
        return "Э";
    case '<':
        return "Б";
    case '>':
        return "Ю";
    case '~':
        return "Ё";
    default: {
        static char singleChar[2] = {0};
        singleChar[0] = key;
        singleChar[1] = 0;
        return singleChar;
    }
    }
}
#endif

#ifdef OLED_UA
static const char *mapUkrainian(char key)
{
    switch (key) {
    case 'g':
        return "ґ";
    case 'i':
        return "і";
    case 'j':
        return "ї";
    case 'u':
        return "є";
    case 'G':
        return "Ґ";
    case 'I':
        return "І";
    case 'J':
        return "Ї";
    case 'U':
        return "Є";
    case 'a':
        return "ф";
    case 'b':
        return "и";
    case 'c':
        return "с";
    case 'd':
        return "в";
    case 'e':
        return "у";
    case 'f':
        return "а";
    case 'h':
        return "р";
    case 'k':
        return "л";
    case 'l':
        return "д";
    case 'm':
        return "ь";
    case 'n':
        return "т";
    case 'o':
        return "щ";
    case 'p':
        return "з";
    case 'q':
        return "й";
    case 'r':
        return "к";
    case 's':
        return "ы";
    case 't':
        return "е";
    case 'v':
        return "м";
    case 'w':
        return "ц";
    case 'x':
        return "ч";
    case 'y':
        return "н";
    case 'z':
        return "я";
    case 'A':
        return "Ф";
    case 'B':
        return "И";
    case 'C':
        return "С";
    case 'D':
        return "В";
    case 'E':
        return "У";
    case 'F':
        return "А";
    case 'H':
        return "Р";
    case 'K':
        return "Л";
    case 'L':
        return "Д";
    case 'M':
        return "Ь";
    case 'N':
        return "Т";
    case 'O':
        return "Щ";
    case 'P':
        return "З";
    case 'Q':
        return "Й";
    case 'R':
        return "К";
    case 'S':
        return "Ы";
    case 'T':
        return "Е";
    case 'V':
        return "М";
    case 'W':
        return "Ц";
    case 'X':
        return "Ч";
    case 'Y':
        return "Н";
    case 'Z':
        return "Я";
    case '[':
        return "х";
    case ']':
        return "ъ";
    case ';':
        return "ж";
    case '\'':
        return "э";
    case ',':
        return "б";
    case '.':
        return "ю";
    case '`':
        return "ё";
    case '{':
        return "Х";
    case '}':
        return "Ъ";
    case ':':
        return "Ж";
    case '"':
        return "Э";
    case '<':
        return "Б";
    case '>':
        return "Ю";
    case '~':
        return "Ё";
    default: {
        static char singleChar[2] = {0};
        singleChar[0] = key;
        singleChar[1] = 0;
        return singleChar;
    }
    }
}
#endif

#ifdef OLED_PL
static const char *mapPolish(char key)
{
    switch (key) {
    case 'a':
        return "ą";
    case 'c':
        return "ć";
    case 'e':
        return "ę";
    case 'l':
        return "ł";
    case 'n':
        return "ń";
    case 'o':
        return "ó";
    case 's':
        return "ś";
    case 'z':
        return "ź";
    case 'x':
        return "ż";
    case 'A':
        return "Ą";
    case 'C':
        return "Ć";
    case 'E':
        return "Ę";
    case 'L':
        return "Ł";
    case 'N':
        return "Ń";
    case 'O':
        return "Ó";
    case 'S':
        return "Ś";
    case 'Z':
        return "Ź";
    case 'X':
        return "Ż";
    default: {
        static char singleChar[2] = {0};
        singleChar[0] = key;
        singleChar[1] = 0;
        return singleChar;
    }
    }
}
#endif

#ifdef OLED_CS
static const char *mapCzech(char key)
{
    switch (key) {
    case 'a':
        return "á";
    case 'c':
        return "č";
    case 'e':
        return "é";
    case 'i':
        return "í";
    case 'n':
        return "ň";
    case 'o':
        return "ó";
    case 'r':
        return "ř";
    case 's':
        return "š";
    case 't':
        return "ť";
    case 'u':
        return "ú";
    case 'y':
        return "ý";
    case 'z':
        return "ž";
    case 'A':
        return "Á";
    case 'C':
        return "Č";
    case 'E':
        return "É";
    case 'I':
        return "Í";
    case 'N':
        return "Ň";
    case 'O':
        return "Ó";
    case 'R':
        return "Ř";
    case 'S':
        return "Š";
    case 'T':
        return "Ť";
    case 'U':
        return "Ú";
    case 'Y':
        return "Ý";
    case 'Z':
        return "Ž";
    default: {
        static char singleChar[2] = {0};
        singleChar[0] = key;
        singleChar[1] = 0;
        return singleChar;
    }
    }
}
#endif

// Build layouts array
static KeyboardLayout layouts[] = {
#ifdef OLED_RU
    {"Ru", mapRussian},
#endif
#ifdef OLED_UA
    {"Ua", mapUkrainian},
#endif
#ifdef OLED_PL
    {"Pl", mapPolish},
#endif
#ifdef OLED_CS
    {"Cs", mapCzech},
#endif
    {"En", mapEnglish},
};

static int currentLayout = 0;
const int KB_LAYOUT_COUNT = sizeof(layouts) / sizeof(layouts[0]);

const char *kb_getCurrentLayoutName(void)
{
    return layouts[currentLayout].name;
}

const char *kb_applyCurrentLayout(char key)
{
    return layouts[currentLayout].mapKey(key);
}

const char *kb_nextLayout(void)
{
    currentLayout = (currentLayout + 1) % KB_LAYOUT_COUNT;
#ifdef LOG_INFO
    LOG_INFO("Switched to: %s", layouts[currentLayout].name);
#endif
    return layouts[currentLayout].name;
}