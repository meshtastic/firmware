#if defined(DMSHELL_SYNTHETIC_SHELL) && DMSHELL_SYNTHETIC_SHELL

#include "DMShellSyntheticShell.h"
#include <string.h>

namespace
{
constexpr char PROMPT[] = "$ ";
constexpr size_t PROMPT_LEN = sizeof(PROMPT) - 1;
constexpr int MAX_ARGS = 8;
constexpr uint8_t CTRL_C = 0x03;
constexpr uint8_t CTRL_D = 0x04;
constexpr uint8_t DEL = 0x7f;
// head's output, cycled; letters and digits only, so it can never contain a marker the client waits for
constexpr char HEAD_PATTERN[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
constexpr uint32_t HEAD_PATTERN_LEN = sizeof(HEAD_PATTERN) - 1;

bool isBlank(uint8_t c)
{
    return c == ' ' || c == '\t';
}

// Splits a command line in place the way sh would for these commands: blanks separate, quotes group.
int splitArgs(char *cmd, char *argv[MAX_ARGS])
{
    int argc = 0;
    char *r = cmd;
    char *w = cmd;
    while (*r) {
        while (isBlank(*r))
            r++;
        if (!*r || argc == MAX_ARGS)
            break;
        argv[argc++] = w;
        while (*r && !isBlank(*r)) {
            if (*r == '\'') {
                for (r++; *r && *r != '\''; *w++ = *r++) {
                }
                if (*r)
                    r++;
            } else if (*r == '"') {
                for (r++; *r && *r != '"'; *w++ = *r++) {
                    if (*r == '\\' && (r[1] == '"' || r[1] == '\\' || r[1] == '$' || r[1] == '`'))
                        r++;
                }
                if (*r)
                    r++;
            } else if (*r == '\\' && r[1]) {
                r++;
                *w++ = *r++;
            } else {
                *w++ = *r++;
            }
        }
        // Step past the separator before terminating, since w may sit on it
        if (*r)
            r++;
        *w++ = '\0';
    }
    return argc;
}

// Decimal only, as the bench types it; false on anything else
bool parseCount(const char *s, uint32_t &out)
{
    if (!s || !*s)
        return false;
    uint32_t value = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9')
            return false;
        const uint32_t digit = (uint32_t)(*s - '0');
        if (value > (UINT32_MAX - digit) / 10)
            return false;
        value = value * 10 + digit;
    }
    out = value;
    return true;
}
} // namespace

void DMShellSyntheticShell::open()
{
    *this = DMShellSyntheticShell{};
    setText(PROMPT, PROMPT_LEN);
}

size_t DMShellSyntheticShell::write(const uint8_t *bytes, size_t len)
{
    size_t taken = 0;
    while (taken < len && takeByte(bytes[taken]))
        taken++;
    return taken;
}

int DMShellSyntheticShell::read(uint8_t *buf, size_t cap)
{
    size_t n = 0;
    while (n < cap) {
        // Output already started goes out whole, as echo cannot land inside one write() on a PTY
        if (headLfOwed) {
            buf[n++] = '\n';
            headLfOwed = false;
        } else if (textPos > 0 && textPos < textLen) {
            const size_t chunk = (size_t)(textLen - textPos) < cap - n ? (size_t)(textLen - textPos) : cap - n;
            memcpy(buf + n, text + textPos, chunk);
            textPos += chunk;
            n += chunk;
        } else if (echoLen > 0) {
            const size_t chunk = echoLen < cap - n ? echoLen : cap - n;
            memcpy(buf + n, echo, chunk);
            memmove(echo, echo + chunk, echoLen - chunk);
            echoLen -= chunk;
            n += chunk;
        } else if (headRemaining > 0) {
            n += readHead(buf + n, cap - n);
        } else if (textPos < textLen) {
            const size_t chunk = (size_t)(textLen - textPos) < cap - n ? (size_t)(textLen - textPos) : cap - n;
            memcpy(buf + n, text + textPos, chunk);
            textPos += chunk;
            n += chunk;
        } else if (!startNext()) {
            break;
        }
    }
    if (n > 0)
        return (int)n;
    return exited ? 0 : -1;
}

// The line discipline: ICRNL, then echo (ECHOCTL for control bytes), then a line goes to the shell at NL
bool DMShellSyntheticShell::takeByte(uint8_t b)
{
    if (exited)
        return true;
    if (b == CTRL_C) {
        interrupt();
        return true;
    }
    if (b == '\r')
        b = '\n';
    if (b == '\n') {
        if (ECHO_CAP - echoLen < 2 || !endLine())
            return false;
        return pushEcho("\r\n", 2);
    }
    if (b == DEL) {
        if (lineLen == 0)
            return true;
        if (!pushEcho("\b \b", 3))
            return false;
        lineLen--;
        return true;
    }
    if (b == CTRL_D) {
        // VEOF on an empty line is end of input, which an interactive sh takes as exit
        if (lineLen > 0)
            return true;
        memcpy(line, "exit", 4);
        lineLen = 4;
        if (endLine())
            return true;
        lineLen = 0;
        return false;
    }
    if (b < 0x20 && b != '\t') {
        const char caret[2] = {'^', (char)(b + 0x40)};
        if (!pushEcho(caret, 2))
            return false;
    } else if (!pushEcho((const char *)&b, 1)) {
        return false;
    }
    if (lineLen < LINE_CAP)
        line[lineLen++] = b;
    return true;
}

bool DMShellSyntheticShell::pushEcho(const char *bytes, size_t len)
{
    if (ECHO_CAP - echoLen < len)
        return false;
    memcpy(echo + echoLen, bytes, len);
    echoLen += len;
    return true;
}

// ISIG with NOFLSH clear: input and output are discarded, the running command dies and the shell reprompts
void DMShellSyntheticShell::interrupt()
{
    lineLen = 0;
    echoLen = 0;
    queueLen = 0;
    trailingEmpty = 0;
    textLen = textPos = 0;
    headRemaining = 0;
    headLfOwed = false;
    pushEcho("^C\r\n", 4);
    setText(PROMPT, PROMPT_LEN);
}

// Queue the finished line for the shell; false, with nothing changed, when the queue has no room for it
bool DMShellSyntheticShell::endLine()
{
    size_t start = 0;
    size_t end = lineLen;
    while (start < end && isBlank(line[start]))
        start++;
    while (end > start && isBlank(line[end - 1]))
        end--;
    if (start == end) {
        trailingEmpty++;
        lineLen = 0;
        return true;
    }
    const size_t len = end - start;
    if (QUEUE_CAP - queueLen < 3 + len)
        return false;
    const uint16_t empties = trailingEmpty > 0xffff ? 0xffff : (uint16_t)trailingEmpty;
    queue[queueLen++] = (uint8_t)(empties & 0xff);
    queue[queueLen++] = (uint8_t)(empties >> 8);
    queue[queueLen++] = (uint8_t)len;
    memcpy(queue + queueLen, line + start, len);
    queueLen += len;
    trailingEmpty = 0;
    lineLen = 0;
    return true;
}

// Hand the shell its next line; false when it is waiting for input
bool DMShellSyntheticShell::startNext()
{
    if (exited)
        return false;
    if (queueLen > 0) {
        uint16_t empties = (uint16_t)(queue[0] | (queue[1] << 8));
        if (empties > 0) {
            empties--;
            queue[0] = (uint8_t)(empties & 0xff);
            queue[1] = (uint8_t)(empties >> 8);
            setText(PROMPT, PROMPT_LEN);
            return true;
        }
        char cmd[LINE_CAP + 1];
        const size_t len = queue[2];
        memcpy(cmd, queue + 3, len);
        cmd[len] = '\0';
        queueLen -= 3 + len;
        memmove(queue, queue + 3 + len, queueLen);
        runCommand(cmd);
        return true;
    }
    if (trailingEmpty > 0) {
        trailingEmpty--;
        setText(PROMPT, PROMPT_LEN);
        return true;
    }
    return false;
}

void DMShellSyntheticShell::runCommand(char *cmd)
{
    char *argv[MAX_ARGS] = {};
    const int argc = splitArgs(cmd, argv);
    setText("", 0);
    if (argc == 0) {
        appendText(PROMPT, PROMPT_LEN, 0);
        return;
    }
    if (strcmp(argv[0], "exit") == 0) {
        exited = true;
        queueLen = 0;
        trailingEmpty = 0;
        return;
    }
    if (strcmp(argv[0], "head") == 0) {
        const char *count = nullptr;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
                count = argv[++i];
            else if (strncmp(argv[i], "-c", 2) == 0 && argv[i][2])
                count = argv[i] + 2;
        }
        uint32_t bytes = 0;
        if (!parseCount(count, bytes)) {
            static const char usage[] = "head: only -c N is supported\r\n";
            appendText(usage, sizeof(usage) - 1);
        } else {
            // Generated as it is read, never buffered; the prompt in text follows it
            headRemaining = bytes;
            headPos = 0;
        }
        appendText(PROMPT, PROMPT_LEN, 0);
        return;
    }
    if (strcmp(argv[0], "printf") == 0 && argc >= 2) {
        // %s and %%, and the escapes \n \t \r \\; the format repeats while arguments remain, as printf(1) does
        int next = 2;
        bool consumed;
        do {
            consumed = false;
            for (const char *f = argv[1]; *f; f++) {
                if (*f == '\\' && f[1]) {
                    f++;
                    if (*f == 'n')
                        appendText("\r\n", 2); // ONLCR
                    else if (*f == 't')
                        appendText("\t", 1);
                    else if (*f == 'r')
                        appendText("\r", 1);
                    else if (*f == '\\')
                        appendText("\\", 1);
                    else {
                        appendText("\\", 1);
                        appendText(f, 1);
                    }
                } else if (*f == '%' && f[1] == '%') {
                    appendText("%", 1);
                    f++;
                } else if (*f == '%' && f[1] == 's') {
                    if (next < argc) {
                        appendText(argv[next], strlen(argv[next]));
                        next++;
                        consumed = true;
                    }
                    f++;
                } else {
                    appendText(f, 1);
                }
            }
        } while (consumed && next < argc);
        appendText(PROMPT, PROMPT_LEN, 0);
        return;
    }
    appendText("sh: ", 4);
    appendText(argv[0], strlen(argv[0]));
    appendText(": not found\r\n", 13);
    appendText(PROMPT, PROMPT_LEN, 0);
}

void DMShellSyntheticShell::setText(const char *bytes, size_t len)
{
    textLen = textPos = 0;
    appendText(bytes, len, 0);
}

// Output past the buffer is dropped, keeping room for the prompt that always ends it
void DMShellSyntheticShell::appendText(const char *bytes, size_t len, size_t reserve)
{
    const size_t room = textLen + reserve < TEXT_CAP ? TEXT_CAP - textLen - reserve : 0;
    if (len > room)
        len = room;
    memcpy(text + textLen, bytes, len);
    textLen += len;
}

// head's N counts the bytes head writes; ONLCR then turns each '\n' into "\r\n", so the wire carries N + N / 72
size_t DMShellSyntheticShell::readHead(uint8_t *buf, size_t cap)
{
    size_t n = 0;
    while (n < cap && headRemaining > 0) {
        if (headPos % HEAD_LINE_LEN == HEAD_LINE_LEN - 1) {
            buf[n++] = '\r';
            if (n == cap)
                headLfOwed = true;
            else
                buf[n++] = '\n';
        } else {
            buf[n++] = (uint8_t)HEAD_PATTERN[headPos % HEAD_PATTERN_LEN];
        }
        headPos++;
        headRemaining--;
    }
    return n;
}

#endif
