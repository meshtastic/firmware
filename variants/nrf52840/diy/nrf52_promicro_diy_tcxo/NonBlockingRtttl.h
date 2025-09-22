#pragma once
// Stub file to avoid compilation errors when ExternalNotification is disabled

class rtttl
{
  public:
    explicit rtttl() {}
    static bool isPlaying() { return false; }
    static void begin(int pin) {}
    static void begin(int pin, const char* song) {}
    static void stop() {}
    static bool done() { return true; }
    static void play() {}
    static void play(const char* song) {}
};