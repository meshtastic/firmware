#pragma once

#include <Arduino.h>
#include <vector>

class ChineseIme
{
  public:
    void setEnabled(bool enabled);
    bool isEnabled() const;

    void reset();
    bool hasBuffer() const;
    const String &buffer() const;
    const std::vector<String> &candidates() const;
    int candidateIndex() const;

    void appendLetter(char c);
    void backspace();
    void moveCandidate(int delta);

    bool commitCandidate(int index, String &out);
    bool commitActive(String &out);

  private:
    void updateCandidates();
    void updateCandidatesFromBuiltin();

    bool enabled =
#if defined(T_DECK_PRO)
        true;
#else
        false;
#endif
    String imeBuffer;
    std::vector<String> imeCandidates;
    int imeCandidateIndex = 0;
    static constexpr size_t kMaxBufferLen = 8;
};
