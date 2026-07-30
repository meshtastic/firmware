#pragma once
#include "BpmfComposer.h"
#include "zhuyin_trie_helpers.h"
#include <string>
#include <vector>

// ═══════════════════════════════════════════════════════════════════
//  bpmf_engine.h  -  one Bopomofo input session
//
//  Holds everything an input method needs to remember between key
//  presses: the syllables being composed, the candidate list they
//  produce, whether that list came from a dictionary lookup or from
//  next-word prediction, and the cache that keeps a redraw from
//  repeating the search. A UI owns an Engine, feeds it keys, draws
//  what it reports, and takes a candidate when the user picks one.
//
//  Nothing here knows about a display, an event queue, or Meshtastic.
//  Together with BpmfComposer.h, zhuyin_trie_helpers.h and the
//  generated dictionary this is the whole input method, and the four
//  files compile against the C++ standard library alone.
//
//  Two ways to drive it, depending on where the composition lives:
//
//    Engine owns it (physical keyboards)
//        engine.addKey('s'); engine.addKey('u'); engine.addKey('6');
//        engine.refresh();
//        display(engine.composingText(), engine.candidates());
//        std::string word = engine.select(idx);
//
//    Caller owns it (the on-screen grid keyboard, whose composition is
//    part of the text buffer it already draws)
//        engine.searchFor(composingSubstring);   // or:
//        engine.predictAfter(lastCommittedChar);
//        display(engine.candidates());
// ═══════════════════════════════════════════════════════════════════

namespace bpmf
{

class Engine
{
  public:
    // ── Composition ──────────────────────────────────────────────
    // Returns true when the key closed a syllable (a tone was entered).
    // A key with no Bopomofo mapping returns false and changes nothing,
    // which is the caller's cue to treat it as English input.
    bool addKey(char ascii)
    {
        const Symbol *sym = lookup_key(ascii);
        return sym ? composer_.addSymbol(*sym) : false;
    }

    bool addSymbol(const Symbol &sym) { return composer_.addSymbol(sym); }

    // Space closes a syllable in the first tone, which carries no mark.
    bool addSpace() { return composer_.addSpace(); }

    void backspace() { composer_.backspace(); }

    void clearComposition()
    {
        composer_.clear();
        clearCandidates();
    }

    bool composing() const { return !composer_.empty(); }

    // With tone marks, for the composition bar.
    std::string composingText() const { return composer_.displayString(); }

    // Tone-stripped, as handed to the dictionary.
    std::string searchText() const { return composer_.searchString(); }

    // ── Candidates ───────────────────────────────────────────────
    // Look up whatever is currently being composed.
    void refresh() { searchFor(composer_.searchString()); }

    // Look up a composition the caller maintains itself. Repeating the
    // same query is free - that is what lets a draw path call this on
    // every frame.
    void searchFor(const std::string &composing)
    {
        if (composing.empty()) {
            clearCandidates();
            return;
        }
        std::string key = "s" + composing;
        if (key == queryKey_)
            return;
        queryKey_   = key;
        prediction_ = false;
        candidates_ = unified_search(composing, maxCandidates_);
    }

    // Offer words continuing the character just committed (中 → 中文).
    // Cached like searchFor, which matters more here: the underlying scan
    // reads the whole candidate blob.
    void predictAfter(const std::string &lastChar)
    {
        std::string key = "p" + lastChar;
        if (key == queryKey_)
            return;
        queryKey_   = key;
        prediction_ = true;
        candidates_ = predict_next(lastChar, maxCandidates_);
    }

    void clearCandidates()
    {
        candidates_.clear();
        queryKey_.clear();
        prediction_ = false;
    }

    const std::vector<std::string> &candidates() const { return candidates_; }
    bool hasCandidates() const { return !candidates_.empty(); }

    // True when the current list came from predictAfter(). The caller needs
    // this on commit: a predicted word repeats the character that produced
    // it, so that character has to give way to the whole word.
    bool isPrediction() const { return prediction_; }

    void setMaxCandidates(int n) { maxCandidates_ = n > 0 ? n : 1; }
    int  maxCandidates() const { return maxCandidates_; }

    // ── Commit ───────────────────────────────────────────────────
    // Take a candidate and end the composition it came from. Returns an
    // empty string if the index is out of range, having changed nothing.
    std::string select(size_t idx)
    {
        if (idx >= candidates_.size())
            return std::string();
        std::string word = candidates_[idx];
        composer_.clear();
        clearCandidates();
        return word;
    }

    // Back to a fresh session.
    void reset()
    {
        composer_.clear();
        clearCandidates();
    }

  private:
    Composer                 composer_;
    std::vector<std::string> candidates_;
    int                      maxCandidates_ = 50;
    bool                     prediction_    = false;
    // Identifies the query behind candidates_. The leading letter keeps the
    // two lookup kinds from ever colliding on the same key.
    std::string queryKey_;
};

} // namespace bpmf
