#if defined(MESHTASTIC_INCLUDE_INKHUD) && defined(MESHTASTIC_INCLUDE_INKHUD_BOOKREADER)

#pragma once

#include "configuration.h"

#include "concurrency/OSThread.h"
#include "graphics/niche/InkHUD/Applet.h"

#include <array>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#ifdef ARCH_ESP32
#include <esp_heap_caps.h>
#endif

namespace NicheGraphics::InkHUD
{

template <typename T> struct ReaderTextAllocator : std::allocator<T> {
    using value_type = T;
    template <typename U> struct rebind {
        using other = ReaderTextAllocator<U>;
    };
    ReaderTextAllocator() = default;
    template <typename U> ReaderTextAllocator(const ReaderTextAllocator<U> &) {}

    T *allocate(size_t count)
    {
#ifdef ARCH_ESP32
        if (count > SIZE_MAX / sizeof(T))
            std::abort();
        void *memory = heap_caps_malloc(count * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!memory)
            memory = heap_caps_malloc(count * sizeof(T), MALLOC_CAP_8BIT);
        if (!memory)
            std::abort();
        return static_cast<T *>(memory);
#else
        return std::allocator<T>::allocate(count);
#endif
    }

    void deallocate(T *memory, size_t count)
    {
#ifdef ARCH_ESP32
        (void)count;
        heap_caps_free(memory);
#else
        std::allocator<T>::deallocate(memory, count);
#endif
    }
};

class BookReaderApplet : public Applet, private concurrency::OSThread
{
  public:
    enum class Mode : uint8_t {
        BROWSER = 0,
        READER = 1,
        CHAPTERS = 2,
        OPENING = 3,
        MENU = 4,
    };

    enum class BookFormat : uint8_t {
        PLAIN_TEXT = 0,
        HTML_LIKE = 1,
        RTF = 2,
        EPUB_CONTAINER = 3,
    };

    struct BrowserEntry {
        std::string name;
        std::string path;
        bool isDir = false;
        uint32_t size = 0;
        BookFormat format = BookFormat::PLAIN_TEXT;
    };
    struct PrefetchEntry {
        int32_t partIndex = -1;
        std::string path;
        std::string text;
        bool truncated = false;

        void clear()
        {
            partIndex = -1;
            path.clear();
            text.clear();
            truncated = false;
        }
    };
    struct ContentsEntry {
        std::string title;
        std::string path;
        std::string fragment;
        uint8_t depth = 0;
        int32_t part = -1;
        int32_t line = -1;
        size_t offset = std::string::npos;
    };
    struct DeferredPartLoad {
        int32_t partIndex = -1;

        void clear() { partIndex = -1; }
    };
    BookReaderApplet();

    void onActivate() override;
    void onDeactivate() override;
    void onBackground() override;
    void onShutdown() override;
    void onRender(bool full) override;
    bool keepScreenAwakeDuringRender() const override { return true; }
    static void setDisplayRefreshActive(bool active);

    void onButtonShortPress() override;
    void onButtonLongPress() override;
    void onExitShort() override;
    void onNavUp() override;
    void onNavDown() override;
    void onNavLeft() override;
    void onNavRight() override;
    bool onTouchPoint(uint16_t x, uint16_t y, bool longPress) override;
    bool onTouchSwipe(int8_t dx, int8_t dy) override;

  private:
    int32_t runOnce() override;
    void ensurePageIndex();
    void resetPageIndex();
    void recordCurrentPartPages();
    void installContents(const std::vector<ContentsEntry> &entries);
    void resolveContentsOffsets(const std::string &path, const std::string &raw, std::string &text,
                                bool *incompleteTable = nullptr);
    void updateContentsLines(int32_t part);
    std::string readingProgress() const;
    std::string readingContext() const;
    void renderReadingProgress();
    uint64_t pageIndexFingerprint() const;
    std::string pageIndexLabel() const;
    std::vector<ContentsEntry> contents;
    std::vector<int32_t> partPageCounts;
    uint16_t pageIndexWidth = 0, pageIndexHeight = 0;
    size_t pageIndexNext = 0;
    std::string pageIndexText;
    size_t pageIndexImageOffset = 0;
    bool pageIndexSaved = false;
    struct PageIndexCache {
        static constexpr uint32_t VERSION = 13;
        uint32_t version = VERSION;
        uint64_t fingerprint = 0;
        uint16_t width = 0, height = 0, count = 0;
        uint8_t textSize = 0;
        std::array<uint16_t, 256> pages{};
    };
    Mode mode = Mode::BROWSER;
    bool initialized = false;
    bool initialLoadArmed = false;
    Mode menuReturnMode = Mode::BROWSER;
    uint8_t menuSelection = 0;
    struct ReadingPosition {
        uint32_t version = 1;
        char path[256] = {};
        int32_t part = -1;
        int32_t line = 0;
        int32_t bookmarkPart = -1;
        int32_t bookmarkLine = -1;
        uint8_t textSize = 0;
        uint8_t bookmarkTextSize = 0;
    } readingPosition;

    std::string currentDir;
    std::vector<BrowserEntry> entries;
    int32_t selectedIndex = 0;
    int32_t firstVisibleIndex = 0;
    bool browserCursorShown = true;
    bool hideTouchBrowserSelectionHighlight = false;
    int32_t pendingOpenIndex = -1;
    int32_t pendingChapterIndex = -1;
    int pendingPageDelta = 0;
    bool openingSplashRendered = false;

    std::string openFilePath;
    std::string bookPath;
    std::string epubCoverImagePath;
    bool useEpubCoverForFirstPart = false;
    bool pageNavigation = false;
    bool openFileTruncated = false;
    bool fileLoaded = false;
    std::string displayText;
    // Retained lines must not consume the internal RAM needed by filesystem/RTOS locks.
    using WrappedLine = std::basic_string<char, std::char_traits<char>, ReaderTextAllocator<char>>;
    std::vector<WrappedLine, ReaderTextAllocator<WrappedLine>> wrappedLines;
    std::vector<uint8_t, ReaderTextAllocator<uint8_t>> wrappedLineStyles;
    std::vector<size_t> chapterOffsets;
    std::vector<int32_t> chapterStartLines;
    std::vector<BrowserEntry> pagedParts;
    int32_t pagedPartIndex = -1;
    bool pagedBookMode = false;
    PrefetchEntry prefetchedNextPart;
    DeferredPartLoad deferredPartLoad;
    bool deferredNextPartPrefetch = false;
    size_t prefetchImageIndex = 0;
    bool backgroundReaderWorkPending = false;
    bool backgroundReaderWorkArmed = false;
    bool deferredWrapExpansion = false;
    size_t deferredWrapImageOffset = 0;
    uint32_t lastReaderInputMs = 0;
    uint32_t lastReaderRenderMs = 0;
    int32_t chapterPickerSelected = 0;
    int32_t chapterPickerFirstVisible = 0;
    bool chapterCursorShown = true;
    bool hideTouchChapterSelectionHighlight = false;
    bool deferInlineImagesUntilNextRender = false;
    std::string imageViewKey;
    std::string expandedImageLine;
    bool expandedImageLoading = false;
    struct InlineImageTarget {
        std::string line;
        int16_t left, top;
        uint16_t width, height;
    };
    std::vector<InlineImageTarget> inlineImageTargets;
    uint16_t imageZoomPermille = 1000;
    bool imageInteractionPending = false;
    bool imagePanOnlyRender = false;
    bool imageViewportFilled = false;
    int16_t imagePanX = 0;
    int16_t imagePanY = 0;
    int32_t scrollLine = 0;
    int32_t linesPerPage = 1;
    uint16_t wrapWidth = 0;

    std::string status;
    bool wrapDirty = true;

    bool ensureInitialDirectory();
    void resetSessionState();
    bool ensureSDReady();
    bool loadDirectory(const std::string &dir);
    bool openSelectedEntry();
    bool loadFile(const BrowserEntry &entry);
    bool loadEpubContainerAsBook(const BrowserEntry &entry);
    bool loadPartText(const BrowserEntry &part, size_t maxBytes, std::string &out, bool &truncatedOut,
                      bool *needsExpansion = nullptr);
    bool loadPagedPart(int32_t partIndex, bool jumpToEndPage, size_t readLimitOverride = 0U, bool allowDeferredExpand = false);
    void prefetchNextPartIfPossible();
    void clearBackgroundReaderState();
    void updateBackgroundReaderPending();
    void queueDeferredPartExpansion(int32_t partIndex);
    void queueDeferredWrapExpansion();
    void queueDeferredNextPartPrefetch();
    bool runBackgroundReaderPass();
    void beginOpenSelectedEntry();
    void moveSelection(int delta);
    void scrollTouchList(int direction);
    void onNavVertical(int direction);
    void onNavHorizontal(int direction);
    uint16_t libraryRowHeight() const;
    void renderListScrollbar(int32_t count, int32_t first, int32_t rows, uint16_t top, uint16_t areaHeight);
    void goToParentDirectory();
    static void ensureSelectionVisible(int32_t selected, int32_t &first, int32_t rowsVisible);

    void rewrapIfNeeded(uint16_t contentWidth, uint16_t contentHeight, bool allowPartialWrap = false);
    void wrapDisplayText(uint16_t contentWidth, size_t maxLines = 0U);
    void scrollByPages(int deltaPages, bool showLoading = true);
    void resetImageView();
    void closeExpandedImage();
    bool currentImagePageLine(std::string *lineOut = nullptr) const;
    bool syncImageViewToCurrentPage();
    bool cycleImageZoom(bool zoomOut);
    bool panImageView(int16_t dx, int16_t dy);
    void queueImageInteractionUpdate(bool panOnly = false);
    void openReaderMenu();
    void selectMenuItem(uint8_t item);
    uint8_t readerMenuItemCount() const;
    void renderReaderMenu();
    void renderReaderHeader(const char *rightLabel);
    void renderImageToolbar();
    AppletFont readingFont();
    void saveReadingPosition();
    void loadReadingPosition();
    void restoreLocation(int32_t part, int32_t line);
    int32_t estimateImageLineSpan(const std::string &imagePath, uint16_t contentWidth, uint16_t lineHeight) const;
    bool drawInlineImage(const std::string &imagePath, int16_t left, int16_t top, uint16_t maxWidth, uint16_t maxHeight,
                         uint16_t zoomPermille = 1000, int16_t panX = 0, int16_t panY = 0,
                         std::vector<uint8_t> *monoBitmap = nullptr, bool useRenderedCache = false, bool preloadOnly = false,
                         bool inlineRaster = false, int16_t clipBottom = -1, bool *prefetchInterrupted = nullptr,
                         uint16_t *bitmapWidth = nullptr, uint16_t *bitmapHeight = nullptr);
    bool prefetchDecodedImages(const std::string &text);
    bool drawInlineImageSequence(const std::vector<std::string> &imagePaths, int16_t left, int16_t top, uint16_t maxWidth,
                                 uint16_t maxHeight, uint16_t zoomPermille = 1000, int16_t panX = 0, int16_t panY = 0,
                                 bool useRenderedCache = false);
    bool jumpToChapter(int32_t chapterIndex);
    void beginJumpToChapter(int32_t chapterIndex);
    int32_t getCurrentChapterIndex() const;
    bool openChapterPicker();
    void closeChapterPicker();

    void renderBrowser();
    void renderOpening(bool full);
    void renderLoading(int16_t left, int16_t top, uint16_t areaWidth, uint16_t areaHeight, bool overlay = true);
    void renderReader(bool completeImages = false, bool imageOnly = false);
    bool pageImagesNeedLoading(int32_t firstLine, int32_t count);
    uint16_t alignedTextOffset(const std::string &line, uint16_t offset, uint8_t alignment);
    void renderStyledText(int16_t x, int16_t y, std::string_view text, uint8_t &style, int16_t underlineY = -1);
    void renderChapterPicker();
};

} // namespace NicheGraphics::InkHUD

#endif
