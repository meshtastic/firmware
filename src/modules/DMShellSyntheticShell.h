#pragma once

// Bench: a stand-in for the PTY and shell behind DMShell, so a board with no PTY can serve the
// throughput test. Plain C++ with no platform headers, so it can be tested off target.

#include <stddef.h>
#include <stdint.h>

/// A terminal with the kernel's default line discipline (ICRNL, ONLCR, ECHO, ECHOCTL, ISIG) in front
/// of a shell that knows `head -c N`, `printf`, `exit` and nothing else.
class DMShellSyntheticShell
{
  public:
    static constexpr size_t LINE_CAP = 96;        ///< Longest command line kept; the rest is echoed and dropped
    static constexpr size_t ECHO_CAP = 128;       ///< Echo waiting to be read
    static constexpr size_t QUEUE_CAP = 192;      ///< Typed-ahead lines waiting for the shell
    static constexpr size_t TEXT_CAP = 128;       ///< One command's short output plus the prompt after it
    static constexpr uint32_t HEAD_LINE_LEN = 72; ///< Source bytes per line of head's output, '\n' included

    /// Start afresh and print the first prompt.
    void open();

    /// Type bytes into the terminal. Returns how many were taken: fewer than len when the echo or the
    /// line queue is full, 0 when nothing fits.
    size_t write(const uint8_t *bytes, size_t len);

    /// Read the terminal's output: >0 bytes, 0 once the shell has exited and everything is read, -1 when
    /// there is nothing yet.
    int read(uint8_t *buf, size_t cap);

  private:
    bool takeByte(uint8_t b);
    bool pushEcho(const char *bytes, size_t len);
    void interrupt();
    bool endLine();
    bool startNext();
    void runCommand(char *cmd);
    void setText(const char *bytes, size_t len);
    /// reserve: room kept for the prompt that ends every command's output
    void appendText(const char *bytes, size_t len, size_t reserve = 2);
    size_t readHead(uint8_t *buf, size_t cap);

    uint8_t line[LINE_CAP] = {};
    uint8_t lineLen = 0;
    uint8_t echo[ECHO_CAP] = {};
    uint8_t echoLen = 0;
    // Each queued command is [empty lines before it, 2 bytes][length][text]; trailingEmpty counts the rest
    uint8_t queue[QUEUE_CAP] = {};
    uint8_t queueLen = 0;
    uint32_t trailingEmpty = 0;
    char text[TEXT_CAP] = {};
    uint8_t textLen = 0;
    uint8_t textPos = 0;
    uint32_t headRemaining = 0; ///< Source bytes head has still to print
    uint32_t headPos = 0;       ///< Source bytes head has printed so far
    bool headLfOwed = false;    ///< ONLCR's "\r" went out at the end of the last read; its "\n" has not
    bool exited = false;
};
