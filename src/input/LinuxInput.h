#pragma once
#if ARCH_PORTDUINO
#include "InputBroker.h"
#include "concurrency/OSThread.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <map>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_EVENTS 10

class LinuxInput : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit LinuxInput(const char *name);
    void deInit(); // Strictly for cleanly "rebooting" the binary on native

  protected:
    virtual int32_t runOnce() override;

  private:
    const char *_originName;
    bool firstTime = 1;
    int shift = 0;
    char key = 0;
    char prevkey = 0;

    InputEvent eventqueue[50]; // The Linux API will return multiple keypresses at a time. Queue them to not miss any.
    int queue_length = 0;
    int queue_progress = 0;

    struct epoll_event events[MAX_EVENTS];
    int fd = -1;
    int ret;
    uint8_t report[8];
    int epollfd;
    struct epoll_event ev;
    uint8_t modifiers = 0;
    std::map<int, char> keymap{
        {KEY_A, 'a'},         {KEY_B, 'b'},           {KEY_C, 'c'},         {KEY_D, 'd'},          {KEY_E, 'e'},
        {KEY_F, 'f'},         {KEY_G, 'g'},           {KEY_H, 'h'},         {KEY_I, 'i'},          {KEY_J, 'j'},
        {KEY_K, 'k'},         {KEY_L, 'l'},           {KEY_M, 'm'},         {KEY_N, 'n'},          {KEY_O, 'o'},
        {KEY_P, 'p'},         {KEY_Q, 'q'},           {KEY_R, 'r'},         {KEY_S, 's'},          {KEY_T, 't'},
        {KEY_U, 'u'},         {KEY_V, 'v'},           {KEY_W, 'w'},         {KEY_X, 'x'},          {KEY_Y, 'y'},
        {KEY_Z, 'z'},         {KEY_BACKSPACE, 0x08},  {KEY_SPACE, ' '},     {KEY_1, '1'},          {KEY_2, '2'},
        {KEY_3, '3'},         {KEY_4, '4'},           {KEY_5, '5'},         {KEY_6, '6'},          {KEY_7, '7'},
        {KEY_8, '8'},         {KEY_9, '9'},           {KEY_0, '0'},         {KEY_DOT, '.'},        {KEY_COMMA, ','},
        {KEY_MINUS, '-'},     {KEY_EQUAL, '='},       {KEY_LEFTBRACE, '['}, {KEY_RIGHTBRACE, ']'}, {KEY_BACKSLASH, '\\'},
        {KEY_SEMICOLON, ';'}, {KEY_APOSTROPHE, '\''}, {KEY_SLASH, '/'},     {KEY_TAB, 0x09}};
    std::map<char, char> uppers{{'a', 'A'}, {'b', 'B'}, {'c', 'C'},  {'d', 'D'}, {'e', 'E'},  {'f', 'F'}, {'g', 'G'}, {'h', 'H'},
                                {'i', 'I'}, {'j', 'J'}, {'k', 'K'},  {'l', 'L'}, {'m', 'M'},  {'n', 'N'}, {'o', 'O'}, {'p', 'P'},
                                {'q', 'Q'}, {'r', 'R'}, {'s', 'S'},  {'t', 'T'}, {'u', 'U'},  {'v', 'V'}, {'w', 'W'}, {'x', 'X'},
                                {'y', 'Y'}, {'z', 'Z'}, {'1', '!'},  {'2', '@'}, {'3', '#'},  {'4', '$'}, {'5', '%'}, {'6', '^'},
                                {'7', '&'}, {'8', '*'}, {'9', '('},  {'0', ')'}, {'.', '>'},  {',', '<'}, {'-', '_'}, {'=', '+'},
                                {'[', '{'}, {']', '}'}, {'\\', '|'}, {';', ':'}, {'\'', '"'}, {'/', '?'}};
};
#endif