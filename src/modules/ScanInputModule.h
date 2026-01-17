#pragma once

#include "configuration.h"
#if HAS_SCREEN && defined(BUTTON_PIN)

#include "modules/SingleButtonInputBase.h"

namespace graphics
{

/**
 * @brief Scan input module for single-button text input
 * 
 * Uses an automatic scanning approach where the system continuously cycles
 * through available options, and the user presses the button when the desired
 * option is highlighted.
 * 
 * Character organization:
 * - 40 characters total: ABCDEFGHIJKLMNOPQRSTUVWXYZ,.?0123456789_
 * - Organized in a 3-level hierarchy:
 *   1. Groups (4 groups of 10 characters each)
 *   2. Subgroups (3 subgroups per group: 3, 3, 4 characters)
 *   3. Individual Characters
 */
class ScanInputModule : public SingleButtonInputBase
{
  public:
    static ScanInputModule &instance();

    void start(const char *header, const char *initialText, uint32_t durationMs,
               std::function<void(const std::string &)> callback) override;
    void draw(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;

  protected:
    void handleButtonPress(uint32_t now) override;
    void handleButtonRelease(uint32_t now, uint32_t duration) override;
    void handleButtonHeld(uint32_t now, uint32_t duration) override;
    void handleIdle(uint32_t now) override;
    
    void handleMenuSelection(int selection) override;
    void handleModeSwitch(int modeIndex) override;
    void drawInterface(OLEDDisplay *display, int16_t x, int16_t y) override;

  private:
    ScanInputModule();
    ~ScanInputModule() = default;

    // Scan state
    enum ScanLevel {
        LEVEL_GROUP = 0,     // Scanning through 4 groups
        LEVEL_SUBGROUP = 1,  // Scanning through 3 subgroups within a group
        LEVEL_CHARACTER = 2  // Scanning through characters within a subgroup
    };
    
    ScanLevel currentLevel = LEVEL_GROUP;
    int currentGroup = 0;       // 0-3
    int currentSubgroup = 0;    // 0-2
    int currentCharIndex = 0;   // Index within subgroup
    
    uint32_t nextScanTime = 0;
    static constexpr uint32_t SCAN_INTERVAL_MS = 800;
    
    // Character organization
    static constexpr const char *CHARACTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ,.?0123456789_";
    static constexpr int CHARS_PER_GROUP = 10;
    static constexpr int GROUPS = 4;
    
    // Subgroup structure: each group has 3 subgroups with [3, 3, 4] characters
    struct SubgroupInfo {
        int startIndex;  // Start index within the group
        int count;       // Number of characters
    };
    
    /**
     * @brief Get subgroup information for the current group
     * @param subgroupIndex The subgroup index (0-2)
     * @return SubgroupInfo structure with start index and count
     */
    SubgroupInfo getSubgroupInfo(int subgroupIndex) const;
    
    /**
     * @brief Get the absolute character index in CHARACTERS string
     * @return The character index, or -1 if invalid
     */
    int getAbsoluteCharIndex() const;
    
    /**
     * @brief Advance the scan to the next position
     */
    void advanceScan();
    
    /**
     * @brief Select the currently highlighted item and drill down
     */
    void selectCurrentItem();
    
    /**
     * @brief Reset scan state to group level
     */
    void resetToGroupLevel();
};

} // namespace graphics

#endif
