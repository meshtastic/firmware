#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <deque>

namespace graphics {

// Small drawing wrapper compatible with BaseUI driver
struct DisplayIface {
  // Implemented by Screen.cpp (see patches section)
  static void clear();
  static void drawText(int x, int y, const char* txt, bool invert = false);
  static int  lineHeight();
  static int  width();
  static int  height();
};

namespace chatui {

enum class Mode : uint8_t { ByNode = 0, ByChannel = 1 };

struct PickerState {
  Mode mode {Mode::ByNode};
  int cursor {0};
  int first {0};
  std::vector<uint32_t> peers;
  std::vector<uint8_t>  chans;
};

struct DetailState {
  bool isChannel {false};
  uint32_t node {0};
  uint8_t channel {0};
  int cursor {0};
  int first {0};
};

// Vista “Chat history”: lista por nodo/canal + detalle con scroll
class ScreenChatHistory {
public:
  void enterPicker(Mode m);
  void renderPicker();
  void handlePickerUp();
  void handlePickerDown();
  bool handlePickerSelect();

  void renderDetail();
  void handleDetailUp();
  void handleDetailDown();

  PickerState& picker() { return picker_; }
  DetailState& detail() { return detail_; }

private:
  PickerState picker_;
  DetailState detail_;

  static int visibleLines();
  static void clampList(int total, int &cursor, int &first, int visible);
  static std::string peerName(uint32_t nodeId);
  static std::string chanName(uint8_t ch);
};

} // namespace chatui
} // namespace graphics
