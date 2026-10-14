#if defined(MESHTASTIC_INCLUDE_INKHUD) && defined(MESHTASTIC_INCLUDE_INKHUD_BOOKREADER)

#include "./BookReaderApplet.h"
#include "./ReaderControls.h"
#include "./ReaderPagination.h"
#include "./SourceBitmap.h"
#include "PowerFSM.h"
#include "Throttle.h"
#include "UptimeClock.h"
#include "graphics/niche/Utils/FlashData.h"
#include "sleep.h"
#if HAS_TOUCHSCREEN
#include "input/TouchScreenImpl1.h"
#endif

#include "ReaderText.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string_view>
#include <utility>
#include <vector>
#if INKHUD_BOOKREADER_GRAYSCALE
#include <esp_heap_caps.h>
#endif

#ifndef INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES
#define INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES 0
#endif

#ifndef INKHUD_BOOKREADER_PERF_LOG
#define INKHUD_BOOKREADER_PERF_LOG 0
#endif

#ifndef INKHUD_BOOKREADER_MAX_FILE_BYTES
#define INKHUD_BOOKREADER_MAX_FILE_BYTES (384U * 1024U)
#endif

#ifndef INKHUD_BOOKREADER_MAX_HTML_FILE_BYTES
#define INKHUD_BOOKREADER_MAX_HTML_FILE_BYTES (256U * 1024U)
#endif

#ifndef INKHUD_BOOKREADER_MAX_OPF_PARSE_BYTES
#define INKHUD_BOOKREADER_MAX_OPF_PARSE_BYTES (256U * 1024U)
#endif

#ifndef INKHUD_BOOKREADER_MAX_TOC_PARSE_BYTES
#define INKHUD_BOOKREADER_MAX_TOC_PARSE_BYTES (256U * 1024U)
#endif

#ifndef INKHUD_BOOKREADER_CSS_MAX_INLINE_TOTAL_BYTES
#define INKHUD_BOOKREADER_CSS_MAX_INLINE_TOTAL_BYTES (96U * 1024U)
#endif

#ifndef INKHUD_BOOKREADER_CSS_MAX_EXTERNAL_FILE_BYTES
#define INKHUD_BOOKREADER_CSS_MAX_EXTERNAL_FILE_BYTES (64U * 1024U)
#endif

#ifndef INKHUD_BOOKREADER_CSS_MAX_EXTERNAL_TOTAL_BYTES
#define INKHUD_BOOKREADER_CSS_MAX_EXTERNAL_TOTAL_BYTES (128U * 1024U)
#endif

#ifndef INKHUD_BOOKREADER_CSS_MAX_SOURCE_TEXT_BYTES
#define INKHUD_BOOKREADER_CSS_MAX_SOURCE_TEXT_BYTES (160U * 1024U)
#endif

#ifndef INKHUD_BOOKREADER_PREFETCH_MAX_FILE_BYTES
#define INKHUD_BOOKREADER_PREFETCH_MAX_FILE_BYTES (160U * 1024U)
#endif

#ifndef INKHUD_BOOKREADER_PREFETCH_MAX_TEXT_BYTES
#define INKHUD_BOOKREADER_PREFETCH_MAX_TEXT_BYTES (192U * 1024U)
#endif

#ifndef INKHUD_BOOKREADER_MAX_EPUB_CENTRAL_DIR_BYTES
#define INKHUD_BOOKREADER_MAX_EPUB_CENTRAL_DIR_BYTES (2U * 1024U * 1024U)
#endif

#ifndef INKHUD_BOOKREADER_MAX_EPUB_COMPRESSED_ENTRY_BYTES
#define INKHUD_BOOKREADER_MAX_EPUB_COMPRESSED_ENTRY_BYTES (2U * 1024U * 1024U)
#endif

#ifndef INKHUD_BOOKREADER_MAX_EPUB_UNCOMPRESSED_ENTRY_BYTES
#define INKHUD_BOOKREADER_MAX_EPUB_UNCOMPRESSED_ENTRY_BYTES (4U * 1024U * 1024U)
#endif

#ifndef INKHUD_BOOKREADER_MAX_EPUB_INLINE_IMAGE_BYTES
#define INKHUD_BOOKREADER_MAX_EPUB_INLINE_IMAGE_BYTES (768U * 1024U)
#endif

#ifndef INKHUD_BOOKREADER_INITIAL_PARTIAL_BYTES
#define INKHUD_BOOKREADER_INITIAL_PARTIAL_BYTES (64U * 1024U)
#endif

#ifndef INKHUD_BOOKREADER_JPEG_MIN_TARGET_DIVISOR
// Subsample only when the decoded image still covers the displayed dimensions.
#define INKHUD_BOOKREADER_JPEG_MIN_TARGET_DIVISOR 1U
#endif

#ifndef INKHUD_BOOKREADER_IMAGE_MIN_HEIGHT_PX
#define INKHUD_BOOKREADER_IMAGE_MIN_HEIGHT_PX 96U
#endif

#ifndef INKHUD_BOOKREADER_IMAGE_DEFAULT_HEIGHT_PX
#define INKHUD_BOOKREADER_IMAGE_DEFAULT_HEIGHT_PX 220U
#endif

#ifndef INKHUD_BOOKREADER_IMAGE_MAX_HEIGHT_PX
#define INKHUD_BOOKREADER_IMAGE_MAX_HEIGHT_PX 420U
#endif

#ifndef INKHUD_BOOKREADER_USE_JPEGDEC
#define INKHUD_BOOKREADER_USE_JPEGDEC 0
#endif

#if INKHUD_BOOKREADER_PERF_LOG
#define BR_PERF_LOG(...) LOG_INFO("[BookReaderPerf] " __VA_ARGS__)
#define BR_FLOW_LOG(...) LOG_DEBUG("[BookReaderFlow] " __VA_ARGS__)
#else
#define BR_PERF_LOG(...) ((void)0)
#define BR_FLOW_LOG(...) ((void)0)
#endif

#if defined(ARCH_ESP32) && defined(HAS_SDCARD) && !defined(SDCARD_USE_SOFT_SPI)
#define INKHUD_BOOKREADER_HAS_SD 1
#include "FSCommon.h"
#include "SPILock.h"
#if INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES
#include <PNGdec.h>
#if INKHUD_BOOKREADER_USE_JPEGDEC
#undef INTELSHORT
#undef INTELLONG
#undef MOTOSHORT
#undef MOTOLONG
#include <JPEGDEC.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#include <rom/tjpgd.h>
#endif
#if defined(ARCH_ESP32)
#include <miniz.h>
#endif
#include <SD.h>
#else
#define INKHUD_BOOKREADER_HAS_SD 0
#endif

using namespace NicheGraphics;

namespace
{

#if INKHUD_BOOKREADER_PERF_LOG
class BookReaderLoadTrace
{
  public:
    explicit BookReaderLoadTrace(const char *stage) : stage(stage), id(++nextId), start(millis())
    {
        BR_PERF_LOG("BEGIN id=%lu stage=%s at=%lu", static_cast<unsigned long>(id), stage, static_cast<unsigned long>(start));
    }
    ~BookReaderLoadTrace()
    {
        BR_PERF_LOG("END id=%lu stage=%s ms=%lu", static_cast<unsigned long>(id), stage,
                    static_cast<unsigned long>(millis() - start));
    }

  private:
    const char *stage;
    uint32_t id;
    uint32_t start;
    static uint32_t nextId;
};
uint32_t BookReaderLoadTrace::nextId = 0;
#define BR_LOAD_SCOPE(stage) BookReaderLoadTrace loadTrace(stage)
#else
#define BR_LOAD_SCOPE(stage)
#endif

#if defined(__GNUC__)
#define BOOKREADER_NOINLINE __attribute__((noinline))
#else
#define BOOKREADER_NOINLINE
#endif

constexpr const char *BOOKS_DIR = "/books";
constexpr uint32_t PARSE_YIELD_EVERY_BYTES = 2048U;
constexpr size_t MAX_FILE_BYTES = INKHUD_BOOKREADER_MAX_FILE_BYTES;
constexpr size_t MAX_HTML_FILE_BYTES = INKHUD_BOOKREADER_MAX_HTML_FILE_BYTES;
constexpr size_t MAX_BROWSER_ENTRIES = 512;
constexpr size_t MAX_OPF_PARSE_BYTES = INKHUD_BOOKREADER_MAX_OPF_PARSE_BYTES;
constexpr size_t MAX_TOC_PARSE_BYTES = INKHUD_BOOKREADER_MAX_TOC_PARSE_BYTES;
constexpr uint16_t BROWSER_ROW_HEIGHT_MIN = 44;
constexpr uint16_t HEADER_BUTTON_MARGIN = 4;
constexpr uint16_t HEADER_BUTTON_BACK_WIDTH = 72;
constexpr uint16_t HEADER_BUTTON_CHAPTER_WIDTH = 112;
constexpr const char *IMAGE_LINE_PREFIX = "[[IMG:";
constexpr const char *IMAGE_SEQUENCE_LINE_PREFIX = "[[IMGSEQ:";
constexpr const char *IMAGE_LINE_SUFFIX = "]]";
constexpr const char *IMAGE_CONTINUATION_LINE = "[[IMG_CONT]]";
constexpr const char *STYLE_BOLD_ON = "[[B+]]";
constexpr const char *STYLE_BOLD_OFF = "[[B-]]";
constexpr const char *STYLE_UNDERLINE_ON = "[[U+]]";
constexpr const char *STYLE_UNDERLINE_OFF = "[[U-]]";
constexpr const char *STYLE_STRIKE_ON = "[[S+]]";
constexpr const char *STYLE_STRIKE_OFF = "[[S-]]";
constexpr const char *LINE_X_OFFSET_PREFIX = "[[X:";
constexpr const char *LINE_X_OFFSET_SUFFIX = "]]";
constexpr const char *ALIGN_CENTER = "[[CENTER]]";
constexpr const char *ALIGN_RIGHT = "[[RIGHT]]";
constexpr uint16_t IMAGE_MIN_HEIGHT_PX = INKHUD_BOOKREADER_IMAGE_MIN_HEIGHT_PX;
constexpr uint16_t IMAGE_MAX_HEIGHT_PX = INKHUD_BOOKREADER_IMAGE_MAX_HEIGHT_PX;
constexpr uint16_t IMAGE_DEFAULT_HEIGHT_PX = INKHUD_BOOKREADER_IMAGE_DEFAULT_HEIGHT_PX;
constexpr uint16_t IMAGE_VERTICAL_PADDING_PX = 2;
constexpr uint16_t IMAGE_ZOOM_NORMAL = 1000;
constexpr uint8_t READER_MENU_ITEMS = 7;
constexpr std::array<uint16_t, 6> IMAGE_ZOOM_LEVELS = {1000, 1200, 1500, 1750, 2600, 3600};
constexpr uint16_t IMAGE_FOOTER_MIN_HEIGHT = InkHUD::ReaderToolbar::height;
constexpr size_t RENDERED_IMAGE_CACHE_MAX_ENTRIES = 4;
constexpr size_t RENDERED_IMAGE_CACHE_MAX_BYTES = 2U * 1024U * 1024U;
#if INKHUD_BOOKREADER_GRAYSCALE
constexpr size_t SOURCE_IMAGE_MAX_BYTES = 2U * 1024U * 1024U;
constexpr size_t SOURCE_IMAGE_CACHE_MAX_BYTES = 3U * 1024U * 1024U;
#else
constexpr size_t SOURCE_IMAGE_MAX_BYTES = 512U * 1024U;
#endif
constexpr int32_t IMAGE_MIN_RESERVED_LINES = 4;
constexpr int32_t IMAGE_MAX_RESERVED_LINES = 64;
constexpr uint16_t STYLE_INDENT_STEP_PX = 18;
constexpr uint16_t STYLE_MAX_INDENT_PX = 220;
constexpr size_t CSS_MAX_INLINE_TOTAL_BYTES = INKHUD_BOOKREADER_CSS_MAX_INLINE_TOTAL_BYTES;
constexpr size_t CSS_MAX_EXTERNAL_FILE_BYTES = INKHUD_BOOKREADER_CSS_MAX_EXTERNAL_FILE_BYTES;
constexpr size_t CSS_MAX_EXTERNAL_TOTAL_BYTES = INKHUD_BOOKREADER_CSS_MAX_EXTERNAL_TOTAL_BYTES;
constexpr size_t CSS_MAX_SOURCE_TEXT_BYTES = INKHUD_BOOKREADER_CSS_MAX_SOURCE_TEXT_BYTES;
constexpr size_t PREFETCH_MAX_FILE_BYTES = INKHUD_BOOKREADER_PREFETCH_MAX_FILE_BYTES;
constexpr size_t PREFETCH_MAX_TEXT_BYTES = INKHUD_BOOKREADER_PREFETCH_MAX_TEXT_BYTES;
constexpr uint32_t BOOKREADER_LOAD_KEEPAWAKE_MS = 1000U;
constexpr const char *EPUB_VIRTUAL_ROOT = "/__epubzip__";
constexpr size_t MAX_EPUB_CENTRAL_DIR_BYTES = INKHUD_BOOKREADER_MAX_EPUB_CENTRAL_DIR_BYTES;
constexpr size_t MAX_EPUB_COMPRESSED_ENTRY_BYTES = INKHUD_BOOKREADER_MAX_EPUB_COMPRESSED_ENTRY_BYTES;
constexpr size_t MAX_EPUB_UNCOMPRESSED_ENTRY_BYTES = INKHUD_BOOKREADER_MAX_EPUB_UNCOMPRESSED_ENTRY_BYTES;
constexpr size_t MAX_EPUB_INLINE_IMAGE_BYTES = INKHUD_BOOKREADER_MAX_EPUB_INLINE_IMAGE_BYTES;
constexpr size_t INITIAL_PARTIAL_BYTES = INKHUD_BOOKREADER_INITIAL_PARTIAL_BYTES;

struct ZipCentralEntry {
    std::string path;
    std::string key;
    uint32_t localHeaderOffset = 0U;
    uint32_t compressedSize = 0U;
    uint32_t uncompressedSize = 0U;
    uint32_t crc = 0U;
    uint16_t method = 0U;
};

struct EpubArchiveCache {
    std::string epubPath;
    std::vector<ZipCentralEntry> entries;
    bool ready = false;

    void clear()
    {
        epubPath.clear();
        entries.clear();
        ready = false;
    }
};

enum class ImageFileType : uint8_t {
    UNKNOWN = 0,
    JPEG = 1,
    PNG = 2,
};

struct ImageProbeResult {
    ImageFileType type = ImageFileType::UNKNOWN;
    uint16_t width = 0;
    uint16_t height = 0;
    bool valid = false;
};

struct ImageProbeCache {
    std::string path;
    ImageProbeResult result;
};

struct RenderedImageCache {
    std::string path;
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t lastUsed = 0;
    std::vector<uint8_t> bitmap;
    bool ready = false;
    int32_t partIndex = -1;
    uint16_t viewportWidth = 0, viewportHeight = 0;
};

std::vector<ImageProbeCache> gImageProbeCache;
std::vector<RenderedImageCache> gRenderedImageCaches;
std::array<RenderedImageCache, 4> gDecodedImages;
uint32_t gDecodedImageClock = 0;
uint32_t gRenderedImageCacheClock = 0;
uint16_t gImageDrawWidth = 0;
uint16_t gImageDrawHeight = 0;
uint16_t gImageDrawZoomPermille = IMAGE_ZOOM_NORMAL;
uint16_t gImageViewportWidth = 0, gImageViewportHeight = 0;

RenderedImageCache *findDecodedImageSlot(const std::string &path, const std::string *protectedText = nullptr)
{
    RenderedImageCache *oldest = nullptr;
    for (auto &entry : gDecodedImages) {
        if (entry.path == path)
            return &entry;
        if (!entry.path.empty() && protectedText && protectedText->find(entry.path) != std::string::npos)
            continue;
        if (!oldest || entry.lastUsed < oldest->lastUsed)
            oldest = &entry;
    }
    return oldest;
}
#if INKHUD_BOOKREADER_GRAYSCALE
bool sourceImagePrefetchFits(size_t incoming, const RenderedImageCache *destination, const std::string &currentText)
{
    for (const auto &entry : gDecodedImages)
        if (&entry != destination && !entry.path.empty() && currentText.find(entry.path) != std::string::npos)
            incoming += entry.bitmap.size();
    return incoming <= SOURCE_IMAGE_CACHE_MAX_BYTES;
}

void trimSourceImages(size_t incoming, const RenderedImageCache *keep = nullptr, const std::string *protectedText = nullptr)
{
    size_t total = incoming;
    for (auto &entry : gDecodedImages)
        if (&entry != keep)
            total += entry.bitmap.size();
    while (total > SOURCE_IMAGE_CACHE_MAX_BYTES) {
        auto oldest = gDecodedImages.end();
        for (auto it = gDecodedImages.begin(); it != gDecodedImages.end(); ++it)
            if (&*it != keep && !it->bitmap.empty() && (!protectedText || protectedText->find(it->path) == std::string::npos) &&
                (oldest == gDecodedImages.end() || it->lastUsed < oldest->lastUsed))
                oldest = it;
        if (oldest == gDecodedImages.end())
            break;
        total -= oldest->bitmap.size();
        *oldest = {};
    }
}

bool allocateImageBitmap(std::vector<uint8_t> &bitmap, size_t bytes, const RenderedImageCache *keep = nullptr,
                         const std::string *protectedText = nullptr)
{
    if (bitmap.capacity() < bytes) {
        if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) < bytes + 131072U) {
            gRenderedImageCaches.clear();
            for (auto &entry : gDecodedImages)
                if (&entry != keep &&
                    (!protectedText || entry.path.empty() || protectedText->find(entry.path) == std::string::npos))
                    entry = {};
        }
        if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) < bytes + 131072U) {
            LOG_WARN("[BookReader] Not enough image memory for %u bytes", static_cast<unsigned>(bytes));
            return false;
        }
    }
    bitmap.assign(bytes, 0);
    return true;
}
#else
bool allocateImageBitmap(std::vector<uint8_t> &bitmap, size_t bytes, const RenderedImageCache * = nullptr,
                         const std::string * = nullptr)
{
    bitmap.assign(bytes, 0);
    return true;
}
#endif
EpubArchiveCache gEpubArchiveCache;
#if INKHUD_BOOKREADER_HAS_SD
File gEpubArchiveFile;
#endif
#if INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES && INKHUD_BOOKREADER_USE_JPEGDEC
JPEGDEC gBookReaderJpegDec;
#endif

void closeCachedEpubFile()
{
#if INKHUD_BOOKREADER_HAS_SD
    concurrency::LockGuard guard(spiLock);
    if (gEpubArchiveFile) {
        gEpubArchiveFile.close();
    }
#endif
}

struct ImageRenderContext {
    InkHUD::BookReaderApplet *applet = nullptr;
    int16_t left = 0;
    int16_t top = 0;
    uint16_t outWidth = 0;
    uint16_t outHeight = 0;
    uint16_t srcWidth = 0;
    uint16_t srcHeight = 0;
    std::vector<uint8_t> *monoBitmap = nullptr;
    std::vector<uint8_t> *sourceBitmap = nullptr;
    bool skipScreenWrite = false; // when true, caller writes the decoded bitmap with writeBitmap()
    bool inlineRaster = false;
    bool *prefetchInterrupted = nullptr;
};

bool readerInputPending()
{
#if HAS_TOUCHSCREEN
#if defined(T5_S3_EPAPER_PRO)
    if (!isTouchInputEnabled())
        return false;
    if (digitalRead(SCREEN_TOUCH_INT) == LOW)
        return true;
#endif
    return touchScreenImpl1 && touchScreenImpl1->isTouchActive();
#else
    return false;
#endif
}

bool interruptImagePrefetch(const ImageRenderContext &ctx)
{
    if (!ctx.prefetchInterrupted || !readerInputPending())
        return false;
    *ctx.prefetchInterrupted = true;
    return true;
}

enum class InlineStyleMarker : uint8_t {
    NONE = 0,
    BOLD_ON,
    BOLD_OFF,
    UNDERLINE_ON,
    UNDERLINE_OFF,
    STRIKE_ON,
    STRIKE_OFF,
};

#if INKHUD_BOOKREADER_HAS_SD && INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES
struct JpegRenderContext {
    const uint8_t *memory = nullptr;
    size_t memorySize = 0U;
    size_t memoryOffset = 0U;
    ImageRenderContext render;
    std::array<uint16_t, 16U * 16U> blockBuffer;
};

#if INKHUD_BOOKREADER_USE_JPEGDEC
struct JpegDecRenderContext : ImageRenderContext {
};

struct EpubJpegStream {
    uint32_t dataOffset = 0U;
    uint32_t dataSize = 0U;
    uint32_t bufferedOffset = 0U;
    size_t bufferedSize = 0U;
    std::vector<uint8_t> buffer;
    File *archive = nullptr;
    std::vector<uint8_t> readAheadBuffer;
    uint32_t readAheadOffset = 0;
    size_t readAheadSize = 0;
    bool readAheadEnabled = false;
    bool readAheadActive = false;
    bool readAheadSucceeded = false;
    std::atomic<bool> readAheadDone{false};
#if INKHUD_BOOKREADER_PERF_LOG
    uint32_t readMs = 0;
    uint32_t readCalls = 0;
    uint32_t readAheadMs = 0;
    uint32_t readAheadHits = 0;
    uint32_t readAheadWaitMs = 0;
#endif

    void finishReadAhead()
    {
        if (!readAheadActive)
            return;
#if INKHUD_BOOKREADER_PERF_LOG
        const uint32_t startMs = Time::getMillis();
#endif
        while (!readAheadDone.load())
            vTaskDelay(1);
        readAheadActive = false;
#if INKHUD_BOOKREADER_PERF_LOG
        readAheadWaitMs += Time::getMillis() - startMs;
        readMs += readAheadMs;
        ++readCalls;
#endif
    }

    ~EpubJpegStream() { finishReadAhead(); }
};

struct JpegPrefetchJob {
    std::string path;
    File archive;
    EpubJpegStream stream;
    JPEGDEC decoder;
    std::vector<uint8_t> bitmap;
    uint16_t width = 0, height = 0;
    unsigned blocksSinceYield = 0;
    uint32_t startedMs = 0;
    uint32_t decodeMs = 0;
    std::atomic<bool> cancelled{false};
    std::atomic<bool> done{false};
    bool succeeded = false;
};

JpegPrefetchJob *gJpegPrefetchJob = nullptr;
std::atomic<bool> gJpegPrefetchPaused{false};
std::atomic<bool> gReaderDisplayRefreshActive{false};

struct JpegPrefetchRenderGuard {
    bool wasPaused;
    explicit JpegPrefetchRenderGuard(bool pause) : wasPaused(gJpegPrefetchPaused.load())
    {
        if (pause)
            gJpegPrefetchPaused.store(true);
    }
    ~JpegPrefetchRenderGuard() { gJpegPrefetchPaused.store(wasPaused); }
};

struct JpegPrefetchSleepGuard {
    int onSleep(void *) { return gJpegPrefetchJob && !gJpegPrefetchJob->done.load() ? 1 : 0; }
    CallbackObserver<JpegPrefetchSleepGuard, void *> observer{this, &JpegPrefetchSleepGuard::onSleep};
    JpegPrefetchSleepGuard() { observer.observe(&preflightSleep); }
};

void clearJpegPrefetchJob()
{
    if (!gJpegPrefetchJob)
        return;
    gJpegPrefetchJob->cancelled.store(true);
    while (!gJpegPrefetchJob->done.load())
        vTaskDelay(1);
    delete gJpegPrefetchJob;
    gJpegPrefetchJob = nullptr;
}

bool collectJpegPrefetch(const std::string &path, bool wait, const std::string &currentText)
{
    auto *job = gJpegPrefetchJob;
    if (!job || job->path != path)
        return false;
    [[maybe_unused]] const uint32_t waitStartedMs = Time::getMillis();
    while (wait && !job->done.load())
        vTaskDelay(1);
    if (!job->done.load())
        return false;
    auto *entry = job->succeeded ? findDecodedImageSlot(path, &currentText) : nullptr;
#if INKHUD_BOOKREADER_GRAYSCALE
    if (entry && !sourceImagePrefetchFits(job->bitmap.size(), entry, currentText))
        entry = nullptr;
#endif
    const bool succeeded = entry != nullptr;
    if (succeeded) {
#if INKHUD_BOOKREADER_GRAYSCALE
        trimSourceImages(job->bitmap.size(), entry, &currentText);
#endif
        *entry = {path, job->width, job->height, ++gDecodedImageClock, std::move(job->bitmap), true};
    }
    BR_PERF_LOG("JPEG worker finished path=%s success=%u decodeMs=%lu waitMs=%lu", path.c_str(), succeeded,
                static_cast<unsigned long>(job->decodeMs), static_cast<unsigned long>(Time::getMillis() - waitStartedMs));
    delete job;
    gJpegPrefetchJob = nullptr;
    return succeeded;
}
#endif

struct PngRenderContext : ImageRenderContext {
    PNG *decoder = nullptr;
    std::vector<uint16_t> lineBuffer;
};
#endif

int hexNibble(char c);
std::string compactTextForDisplay(const std::string &input, std::vector<std::pair<std::string, size_t>> *anchors = nullptr);
std::string decodeHtmlEntity(const std::string &entity);
bool isImageMarkerLine(const std::string &line, std::string *pathOut = nullptr);
std::string makeImageMarkerLine(const std::string &imagePath);
bool isImageSequenceMarkerLine(const std::string &line, std::vector<std::string> *pathsOut = nullptr);
std::string makeImageSequenceMarkerLine(const std::vector<std::string> &imagePaths);
ImageProbeResult probeImageFile(const std::string &path);
bool isEpubVirtualPath(const std::string &path);
std::string toEpubVirtualPath(const std::string &entryPath);
std::string epubVirtualPathToEntryPath(const std::string &virtualPath);
#if INKHUD_BOOKREADER_HAS_SD
bool parseZipCentralDirectory(const std::string &zipPath, std::vector<ZipCentralEntry> &entriesOut, File &openFileOut);
#endif
bool ensureEpubArchiveCached(const std::string &epubPath);
bool readEpubEntryBinaryLimited(const std::string &entryPath, size_t maxBytes, std::vector<uint8_t> &out, bool &truncatedOut);

bool readEpubEntryLimited(const std::string &entryPath, size_t maxBytes, std::string &out, bool &truncatedOut);
bool readEpubVirtualTextFileLimited(const std::string &path, size_t maxBytes, std::string &out, bool *truncatedOut);
bool readEpubVirtualBinaryFileLimited(const std::string &path, size_t maxBytes, std::vector<uint8_t> &out, bool *truncatedOut);
bool inflateDeflateRawHeapBacked(const uint8_t *src, size_t srcLen, uint8_t *dst, size_t dstCap, size_t &outLen,
                                 bool prefixOnly = false, bool inputHasMore = false);
bool parseEpubRootfilePath(const std::string &containerXml, std::string &opfPathOut);
void parseOpfTocCandidates(const std::string &opfText, const std::string &opfDir, std::vector<std::string> &tocPathsOut);
std::string pickFallbackOpfFromZip(const std::vector<ZipCentralEntry> &entries);
#if INKHUD_BOOKREADER_HAS_SD && INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES
void drawMonochromeScaledBlock(ImageRenderContext &ctx, const uint16_t *pixels, int srcXStart, int srcYStart, int srcBlockW,
                               int srcBlockH);
void drawMonochromeScaledBlockStride(ImageRenderContext &ctx, const uint16_t *pixels, int srcXStart, int srcYStart, int srcBlockW,
                                     int srcBlockH, int srcStride);
#endif

bool indexingReaderPages = false;

inline void noteBookReaderLoadActivity(bool force = false)
{
    if (indexingReaderPages)
        return;
    // Keep screen-timeout from firing while a book is actively loading/parsing.
    static uint32_t lastKeepAwakeMs = 0;
    if (force || Throttle::hasElapsed(lastKeepAwakeMs, BOOKREADER_LOAD_KEEPAWAKE_MS)) {
        powerFSM.trigger(EVENT_INPUT);
        lastKeepAwakeMs = Time::skipZero(Time::getMillis());
    }
}

inline void yieldDuringBookReaderWork(uint32_t &checkpoint, uint32_t current)
{
    if ((current - checkpoint) >= PARSE_YIELD_EVERY_BYTES) {
        noteBookReaderLoadActivity();
        yield();
        checkpoint = current;
    }
}

size_t maxReadBytesForFormat(InkHUD::BookReaderApplet::BookFormat format)
{
    if (format == InkHUD::BookReaderApplet::BookFormat::HTML_LIKE) {
        return MAX_HTML_FILE_BYTES;
    }
    return MAX_FILE_BYTES;
}

std::string toLowerCopy(const std::string &input)
{
    std::string out = input;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string extensionOfPath(const std::string &path)
{
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return "";
    }
    return toLowerCopy(path.substr(dot));
}

static bool useTouchFriendlyBookReaderLayout(const InkHUD::InkHUD *inkhud)
{
    return inkhud != nullptr && inkhud->hasTouchEnabledProvider();
}

bool hasRenderableImageExtension(const std::string &path)
{
#if !INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES
    (void)path;
    return false;
#else
    const std::string ext = extensionOfPath(path);
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png";
#endif
}

std::string makeImageMarkerLine(const std::string &imagePath)
{
    if (imagePath.empty()) {
        return "";
    }
    return std::string(IMAGE_LINE_PREFIX) + imagePath + IMAGE_LINE_SUFFIX;
}

bool isImageMarkerLine(const std::string &line, std::string *pathOut)
{
    const size_t prefixLen = std::strlen(IMAGE_LINE_PREFIX);
    const size_t suffixLen = std::strlen(IMAGE_LINE_SUFFIX);
    if (line.size() <= prefixLen + suffixLen) {
        return false;
    }
    if (line.rfind(IMAGE_LINE_PREFIX, 0) != 0) {
        return false;
    }
    if (line.compare(line.size() - suffixLen, suffixLen, IMAGE_LINE_SUFFIX) != 0) {
        return false;
    }

    if (pathOut) {
        *pathOut = line.substr(prefixLen, line.size() - prefixLen - suffixLen);
    }
    return true;
}

std::string makeImageSequenceMarkerLine(const std::vector<std::string> &imagePaths)
{
    if (imagePaths.empty()) {
        return "";
    }

    std::string out(IMAGE_SEQUENCE_LINE_PREFIX);
    bool appended = false;
    for (const std::string &imagePath : imagePaths) {
        if (imagePath.empty()) {
            continue;
        }
        if (appended) {
            out.push_back('|');
        }
        out += imagePath;
        appended = true;
    }

    if (!appended) {
        return "";
    }
    out += IMAGE_LINE_SUFFIX;
    return out;
}

bool isImageSequenceMarkerLine(const std::string &line, std::vector<std::string> *pathsOut)
{
    const size_t prefixLen = std::strlen(IMAGE_SEQUENCE_LINE_PREFIX);
    const size_t suffixLen = std::strlen(IMAGE_LINE_SUFFIX);
    if (line.size() <= prefixLen + suffixLen) {
        return false;
    }
    if (line.rfind(IMAGE_SEQUENCE_LINE_PREFIX, 0) != 0) {
        return false;
    }
    if (line.compare(line.size() - suffixLen, suffixLen, IMAGE_LINE_SUFFIX) != 0) {
        return false;
    }

    if (pathsOut) {
        pathsOut->clear();
        const std::string payload = line.substr(prefixLen, line.size() - prefixLen - suffixLen);
        size_t start = 0;
        while (start <= payload.size()) {
            const size_t sep = payload.find('|', start);
            const size_t end = (sep == std::string::npos) ? payload.size() : sep;
            const std::string part = payload.substr(start, end - start);
            if (!part.empty()) {
                pathsOut->push_back(part);
            }
            if (sep == std::string::npos) {
                break;
            }
            start = sep + 1U;
        }
        if (pathsOut->empty()) {
            return false;
        }
    }
    return true;
}

bool isImageContinuationLine(const std::string &line)
{
    return line == IMAGE_CONTINUATION_LINE;
}

InlineStyleMarker parseInlineStyleMarkerAt(std::string_view line, size_t start, size_t &len)
{
    len = 0;
    if (start >= line.size() || line[start] != '[') {
        return InlineStyleMarker::NONE;
    }
    if (line.compare(start, std::strlen(STYLE_BOLD_ON), STYLE_BOLD_ON) == 0) {
        len = std::strlen(STYLE_BOLD_ON);
        return InlineStyleMarker::BOLD_ON;
    }
    if (line.compare(start, std::strlen(STYLE_BOLD_OFF), STYLE_BOLD_OFF) == 0) {
        len = std::strlen(STYLE_BOLD_OFF);
        return InlineStyleMarker::BOLD_OFF;
    }
    if (line.compare(start, std::strlen(STYLE_UNDERLINE_ON), STYLE_UNDERLINE_ON) == 0) {
        len = std::strlen(STYLE_UNDERLINE_ON);
        return InlineStyleMarker::UNDERLINE_ON;
    }
    if (line.compare(start, std::strlen(STYLE_UNDERLINE_OFF), STYLE_UNDERLINE_OFF) == 0) {
        len = std::strlen(STYLE_UNDERLINE_OFF);
        return InlineStyleMarker::UNDERLINE_OFF;
    }
    if (line.compare(start, std::strlen(STYLE_STRIKE_ON), STYLE_STRIKE_ON) == 0) {
        len = std::strlen(STYLE_STRIKE_ON);
        return InlineStyleMarker::STRIKE_ON;
    }
    if (line.compare(start, std::strlen(STYLE_STRIKE_OFF), STYLE_STRIKE_OFF) == 0) {
        len = std::strlen(STYLE_STRIKE_OFF);
        return InlineStyleMarker::STRIKE_OFF;
    }
    return InlineStyleMarker::NONE;
}

uint8_t applyInlineStyleMarker(uint8_t style, InlineStyleMarker marker)
{
    switch (marker) {
    case InlineStyleMarker::BOLD_ON:
        return style | 1U;
    case InlineStyleMarker::BOLD_OFF:
        return style & ~1U;
    case InlineStyleMarker::UNDERLINE_ON:
        return style | 2U;
    case InlineStyleMarker::UNDERLINE_OFF:
        return style & ~2U;
    case InlineStyleMarker::STRIKE_ON:
        return style | 4U;
    case InlineStyleMarker::STRIKE_OFF:
        return style & ~4U;
    default:
        return style;
    }
}

uint8_t inlineStyleAfter(std::string_view text, uint8_t style)
{
    for (size_t pos = 0; pos < text.size();) {
        size_t length = 0;
        const auto marker = parseInlineStyleMarkerAt(text, pos, length);
        style = applyInlineStyleMarker(style, marker);
        pos += length ? length : 1;
    }
    return style;
}

std::string inlineStylePrefix(uint8_t style)
{
    std::string prefix;
    if (style & 1U)
        prefix += STYLE_BOLD_ON;
    if (style & 2U)
        prefix += STYLE_UNDERLINE_ON;
    if (style & 4U)
        prefix += STYLE_STRIKE_ON;
    return prefix;
}

std::string stripInlineStyleMarkers(const std::string &line)
{
    std::string out;
    out.reserve(line.size());
    size_t pos = 0;
    while (pos < line.size()) {
        size_t markerLen = 0;
        if (parseInlineStyleMarkerAt(line, pos, markerLen) != InlineStyleMarker::NONE && markerLen > 0) {
            pos += markerLen;
            continue;
        }
        out.push_back(line[pos]);
        pos++;
    }
    return out;
}

std::string makeLineXOffsetMarker(uint16_t xOffset)
{
    if (xOffset == 0) {
        return "";
    }
    return std::string(LINE_X_OFFSET_PREFIX) + std::to_string(xOffset) + LINE_X_OFFSET_SUFFIX;
}

bool parseLineXOffsetMarkerAt(std::string_view line, size_t start, uint16_t &xOffset, size_t &len)
{
    xOffset = 0;
    len = 0;
    const size_t prefixLen = std::strlen(LINE_X_OFFSET_PREFIX);
    const size_t suffixLen = std::strlen(LINE_X_OFFSET_SUFFIX);
    if (start + prefixLen + suffixLen >= line.size()) {
        return false;
    }
    if (line.compare(start, prefixLen, LINE_X_OFFSET_PREFIX) != 0) {
        return false;
    }

    size_t end = line.find(LINE_X_OFFSET_SUFFIX, start + prefixLen);
    if (end == std::string::npos) {
        return false;
    }

    uint32_t value = 0;
    for (size_t i = start + prefixLen; i < end; i++) {
        const char c = line[i];
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
        value = (value * 10U) + static_cast<uint32_t>(c - '0');
        if (value > STYLE_MAX_INDENT_PX) {
            value = STYLE_MAX_INDENT_PX;
        }
    }

    len = (end + suffixLen) - start;
    xOffset = static_cast<uint16_t>(value);
    return true;
}

uint16_t consumeLeadingLineXOffsetMarkers(std::string_view line, size_t &contentStart, uint8_t *alignment = nullptr)
{
    contentStart = 0;
    uint32_t total = 0;
    while (contentStart < line.size()) {
        const bool centered = line.compare(contentStart, std::strlen(ALIGN_CENTER), ALIGN_CENTER) == 0;
        const bool right = line.compare(contentStart, std::strlen(ALIGN_RIGHT), ALIGN_RIGHT) == 0;
        if (centered || right) {
            if (alignment)
                *alignment = centered ? 2 : 3;
            contentStart += std::strlen(centered ? ALIGN_CENTER : ALIGN_RIGHT);
            continue;
        }
        uint16_t markerOffset = 0;
        size_t markerLen = 0;
        if (!parseLineXOffsetMarkerAt(line, contentStart, markerOffset, markerLen) || markerLen == 0) {
            break;
        }
        total += markerOffset;
        if (total > STYLE_MAX_INDENT_PX) {
            total = STYLE_MAX_INDENT_PX;
        }
        contentStart += markerLen;
    }
    return static_cast<uint16_t>(total);
}

uint16_t toMonoColor(uint16_t rgb565)
{
    const uint16_t r = (rgb565 >> 11) & 0x1F;
    const uint16_t g = (rgb565 >> 5) & 0x3F;
    const uint16_t b = rgb565 & 0x1F;
    const uint32_t luma = static_cast<uint32_t>(r) * 38U + static_cast<uint32_t>(g) * 75U + static_cast<uint32_t>(b) * 15U;
    return (luma >= 3300U) ? static_cast<uint16_t>(NicheGraphics::InkHUD::WHITE)
                           : static_cast<uint16_t>(NicheGraphics::InkHUD::BLACK);
}

bool hasSupportedBookExtension(const std::string &path)
{
    const std::string ext = extensionOfPath(path);
    return ext == ".epub" || ext == ".kepub" || ext == ".epu";
}

bool hasSupportedContinuousChapterExtension(const std::string &path)
{
    const std::string ext = extensionOfPath(path);
    return ext == ".txt" || ext == ".text" || ext == ".md" || ext == ".html" || ext == ".htm" || ext == ".xhtml" ||
           ext == ".xml" || ext == ".fb2" || ext == ".rtf";
}

void getHeaderButtonRect(uint16_t screenWidth, uint16_t headerHeight, bool right, uint16_t preferredWidth, int16_t &left,
                         int16_t &top, uint16_t &width, uint16_t &height)
{
    height = (headerHeight > 4) ? (headerHeight - 4) : headerHeight;
    width = std::min<uint16_t>(preferredWidth, (screenWidth > 16) ? (screenWidth / 2) : preferredWidth);
    top = (headerHeight > height) ? static_cast<int16_t>((headerHeight - height) / 2) : 0;
    left = right ? static_cast<int16_t>(screenWidth - HEADER_BUTTON_MARGIN - width) : static_cast<int16_t>(HEADER_BUTTON_MARGIN);
}

bool pointInRect(uint16_t x, uint16_t y, int16_t left, int16_t top, uint16_t width, uint16_t height)
{
    return (x >= left) && (x < (left + width)) && (y >= top) && (y < (top + height));
}

size_t monoBitmapBytes(uint16_t width, uint16_t height)
{
    return InkHUD::readerBitmapStride(width) * static_cast<size_t>(height);
}

#if !INKHUD_BOOKREADER_GRAYSCALE
bool monoBitmapBlackAt(const std::vector<uint8_t> &bitmap, uint16_t width, uint16_t x, uint16_t y)
{
    const size_t stride = (static_cast<size_t>(width) + 7U) / 8U;
    const size_t byteIndex = static_cast<size_t>(y) * stride + (static_cast<size_t>(x) / 8U);
    if (byteIndex >= bitmap.size()) {
        return false;
    }
    return (bitmap[byteIndex] & static_cast<uint8_t>(0x80U >> (x & 0x7U))) != 0U;
}

void setMonoBitmapBlack(std::vector<uint8_t> &bitmap, uint16_t width, uint16_t x, uint16_t y, bool black)
{
    const size_t stride = (static_cast<size_t>(width) + 7U) / 8U;
    const size_t byteIndex = static_cast<size_t>(y) * stride + (static_cast<size_t>(x) / 8U);
    if (byteIndex >= bitmap.size()) {
        return;
    }
    const uint8_t bitMask = static_cast<uint8_t>(0x80U >> (x & 0x7U));
    if (black) {
        bitmap[byteIndex] |= bitMask;
    } else {
        bitmap[byteIndex] &= static_cast<uint8_t>(~bitMask);
    }
}

#endif

void copyMonoBitmapInto(const std::vector<uint8_t> &src, uint16_t srcWidth, uint16_t srcHeight, std::vector<uint8_t> &dst,
                        uint16_t dstWidth, uint16_t dstHeight, int16_t dstLeft, int16_t dstTop)
{
    if (src.empty() || dst.empty() || srcWidth == 0U || srcHeight == 0U || dstWidth == 0U || dstHeight == 0U) {
        return;
    }
    if (src.size() < monoBitmapBytes(srcWidth, srcHeight) || dst.size() < monoBitmapBytes(dstWidth, dstHeight))
        return;
    if (dstLeft == 0 && srcWidth == dstWidth && dstTop >= 0 && dstTop < dstHeight) {
        const size_t stride = InkHUD::readerBitmapStride(srcWidth);
        const size_t rows = std::min<uint16_t>(srcHeight, dstHeight - dstTop);
        const size_t offset = static_cast<size_t>(dstTop) * stride;
        if (src.size() >= rows * stride && dst.size() >= offset + rows * stride) {
            std::memcpy(dst.data() + offset, src.data(), rows * stride);
            return;
        }
    }
    for (uint16_t sy = 0; sy < srcHeight; sy++) {
        const int32_t dy = static_cast<int32_t>(dstTop) + sy;
        if (dy < 0 || dy >= dstHeight) {
            continue;
        }
        for (uint16_t sx = 0; sx < srcWidth; sx++) {
            const int32_t dx = static_cast<int32_t>(dstLeft) + sx;
            if (dx < 0 || dx >= dstWidth) {
                continue;
            }
#if INKHUD_BOOKREADER_GRAYSCALE
            InkHUD::setReaderGray(dst.data(), InkHUD::readerBitmapStride(dstWidth), dx, dy,
                                  InkHUD::readerGrayAt(src.data(), InkHUD::readerBitmapStride(srcWidth), sx, sy));
#else
            if (monoBitmapBlackAt(src, srcWidth, sx, sy)) {
                setMonoBitmapBlack(dst, dstWidth, static_cast<uint16_t>(dx), static_cast<uint16_t>(dy), true);
            }
#endif
        }
    }
}

size_t renderedImageCacheBytes()
{
    size_t total = 0;
    for (const RenderedImageCache &entry : gRenderedImageCaches) {
        if (entry.ready) {
            total += entry.bitmap.size();
        }
    }
    return total;
}

void clearRenderedImageCaches()
{
#if INKHUD_BOOKREADER_HAS_SD && INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES && INKHUD_BOOKREADER_USE_JPEGDEC
    clearJpegPrefetchJob();
#endif
    gRenderedImageCaches.clear();
    gDecodedImages = {};
    gDecodedImageClock = 0;
    gRenderedImageCacheClock = 0;
    gImageDrawWidth = gImageDrawHeight = 0;
}

std::string imageCacheKey(const std::string &path, uint16_t width, uint16_t height, uint16_t zoom, bool inlineRaster)
{
    return std::string(inlineRaster ? "inline:" : "img:") + std::to_string(width) + "x" + std::to_string(height) + ":" +
           std::to_string(zoom) + "|" + path;
}

std::string imageSequenceCacheKey(const std::vector<std::string> &paths, uint16_t width, uint16_t height, uint16_t zoom)
{
    std::string key = "seq:0:" + std::to_string(width) + "x" + std::to_string(height) + ":" + std::to_string(zoom);
    for (const auto &path : paths)
        key += "|" + path;
    return key;
}

RenderedImageCache *findRenderedImageCache(const std::string &cacheKey)
{
    for (RenderedImageCache &entry : gRenderedImageCaches) {
        if (entry.ready && entry.path == cacheKey) {
            entry.lastUsed = ++gRenderedImageCacheClock;
            return &entry;
        }
    }
    return nullptr;
}

void trimRenderedImageCaches()
{
    while (gRenderedImageCaches.size() > 1U && (gRenderedImageCaches.size() > RENDERED_IMAGE_CACHE_MAX_ENTRIES ||
                                                renderedImageCacheBytes() > RENDERED_IMAGE_CACHE_MAX_BYTES)) {
        auto oldest =
            std::min_element(gRenderedImageCaches.begin(), gRenderedImageCaches.end(),
                             [](const RenderedImageCache &a, const RenderedImageCache &b) { return a.lastUsed < b.lastUsed; });
        if (oldest == gRenderedImageCaches.end()) {
            break;
        }
        BR_PERF_LOG("trimRenderedImageCache key=%s bytes=%u", oldest->path.c_str(), static_cast<unsigned>(oldest->bitmap.size()));
        gRenderedImageCaches.erase(oldest);
    }
}

void storeRenderedImageCache(const std::string &cacheKey, uint16_t width, uint16_t height, std::vector<uint8_t> &&bitmap)
{
    if (cacheKey.empty() || width == 0U || height == 0U || bitmap.empty()) {
        return;
    }

    if (RenderedImageCache *existing = findRenderedImageCache(cacheKey)) {
        existing->width = width;
        existing->height = height;
        existing->bitmap = std::move(bitmap);
        existing->ready = true;
        existing->lastUsed = ++gRenderedImageCacheClock;
    } else {
        RenderedImageCache entry;
        entry.path = cacheKey;
        entry.width = width;
        entry.height = height;
        entry.bitmap = std::move(bitmap);
        entry.ready = true;
        entry.lastUsed = ++gRenderedImageCacheClock;
        gRenderedImageCaches.push_back(std::move(entry));
    }
    trimRenderedImageCaches();
}

bool drawRenderedImageCache(InkHUD::BookReaderApplet *applet, const std::string &cacheKey, int16_t left, int16_t top,
                            uint16_t maxWidth, uint16_t maxHeight, uint16_t zoomPermille, int16_t panX, int16_t panY,
                            int16_t clipBottom = -1)
{
    RenderedImageCache *cache = findRenderedImageCache(cacheKey);
    if (!applet || !cache || cache->width == 0U || cache->height == 0U || cache->bitmap.empty() || maxWidth == 0U ||
        maxHeight == 0U) {
        return false;
    }

    const uint16_t drawWidth = cache->width;
    const uint16_t drawHeight = cache->height;
    gImageDrawWidth = drawWidth;
    gImageDrawHeight = drawHeight;
    gImageDrawZoomPermille = zoomPermille;
    gImageViewportWidth = maxWidth;
    gImageViewportHeight = maxHeight;
    const int32_t maxPanX = (drawWidth > maxWidth) ? static_cast<int32_t>((drawWidth - maxWidth) / 2U) : 0;
    const int32_t maxPanY = (drawHeight > maxHeight) ? static_cast<int32_t>((drawHeight - maxHeight) / 2U) : 0;
    const int32_t clampedPanX = std::clamp<int32_t>(panX, -maxPanX, maxPanX);
    const int32_t clampedPanY = std::clamp<int32_t>(panY, -maxPanY, maxPanY);
    const int16_t imageLeft =
        static_cast<int16_t>(static_cast<int32_t>(left) + ((static_cast<int32_t>(maxWidth) - drawWidth) / 2) - clampedPanX);
    const int16_t imageTop =
        static_cast<int16_t>(static_cast<int32_t>(top) + ((static_cast<int32_t>(maxHeight) - drawHeight) / 2) - clampedPanY);

    const size_t sourceStride = InkHUD::readerBitmapStride(cache->width);
    const int32_t visibleLeft = std::max<int32_t>(left, imageLeft);
    const int32_t visibleTop = std::max<int32_t>(top, imageTop);
    const int32_t visibleRight =
        std::min<int32_t>(static_cast<int32_t>(left) + maxWidth, static_cast<int32_t>(imageLeft) + drawWidth);
    int32_t visibleBottom = std::min<int32_t>(static_cast<int32_t>(top) + maxHeight, static_cast<int32_t>(imageTop) + drawHeight);
    if (clipBottom >= 0)
        visibleBottom = std::min<int32_t>(visibleBottom, clipBottom);
    if (visibleLeft >= visibleRight || visibleTop >= visibleBottom) {
        return true;
    }
    const uint16_t visibleWidth = static_cast<uint16_t>(visibleRight - visibleLeft);
    const uint16_t visibleHeight = static_cast<uint16_t>(visibleBottom - visibleTop);
    const uint16_t sourceX = static_cast<uint16_t>(visibleLeft - imageLeft);
    const uint16_t sourceY = static_cast<uint16_t>(visibleTop - imageTop);

    // Convert applet-local image position to screen coords, then use the
    // renderer's fast 1-bpp blit path.
    InkHUD::Tile *tile = applet->getTile();
    const int16_t tileLeft = tile ? tile->getLeft() : 0;
    const int16_t tileTop = tile ? tile->getTop() : 0;
    const int16_t screenLeft = static_cast<int16_t>(tileLeft + visibleLeft);
    const int16_t screenTop = static_cast<int16_t>(tileTop + visibleTop);

    [[maybe_unused]] const uint32_t tStart = millis();
#if INKHUD_BOOKREADER_GRAYSCALE
    InkHUD::InkHUD::getInstance()->writeGrayBitmap(screenLeft, screenTop, visibleWidth, visibleHeight, cache->bitmap.data(),
                                                   sourceStride, sourceX, sourceY);
#else
    InkHUD::InkHUD::getInstance()->writeBitmap(screenLeft, screenTop, visibleWidth, visibleHeight, cache->bitmap.data(),
                                               sourceStride, sourceX, sourceY);
#endif
    BR_PERF_LOG("drawRenderedImageCache key=%s src=%ux%u dst=%ux%u zoom=%u pan=%d,%d "
                "ms=%lu",
                cacheKey.c_str(), static_cast<unsigned>(drawWidth), static_cast<unsigned>(drawHeight),
                static_cast<unsigned>(visibleWidth), static_cast<unsigned>(visibleHeight), static_cast<unsigned>(zoomPermille),
                static_cast<int>(panX), static_cast<int>(panY), static_cast<unsigned long>(millis() - tStart));
    return true;
}

InkHUD::BookReaderApplet::BookFormat formatForPath(const std::string &path)
{
    const std::string ext = extensionOfPath(path);

    if (ext == ".txt" || ext == ".text" || ext == ".md") {
        return InkHUD::BookReaderApplet::BookFormat::PLAIN_TEXT;
    }
    if (ext == ".html" || ext == ".htm" || ext == ".xhtml" || ext == ".xml" || ext == ".fb2") {
        return InkHUD::BookReaderApplet::BookFormat::HTML_LIKE;
    }
    if (ext == ".rtf") {
        return InkHUD::BookReaderApplet::BookFormat::RTF;
    }
    if (ext == ".epub" || ext == ".kepub" || ext == ".epu") {
        return InkHUD::BookReaderApplet::BookFormat::EPUB_CONTAINER;
    }
    return InkHUD::BookReaderApplet::BookFormat::PLAIN_TEXT;
}

std::string joinPath(const std::string &base, const std::string &name)
{
    if (name.empty()) {
        return base;
    }
    if (name[0] == '/') {
        return name;
    }
    if (base.empty() || base == "/") {
        return "/" + name;
    }
    return base + "/" + name;
}

std::string normalizeDir(const std::string &path)
{
    if (path.empty()) {
        return "/";
    }
    std::string out = path;
    if (out[0] != '/') {
        out = "/" + out;
    }
    while (out.size() > 1 && out.back() == '/') {
        out.pop_back();
    }
    return out;
}

std::string parentDir(const std::string &path)
{
    std::string p = normalizeDir(path);
    if (p == "/") {
        return "/";
    }
    const size_t slash = p.find_last_of('/');
    if (slash == std::string::npos || slash == 0) {
        return "/";
    }
    return p.substr(0, slash);
}

std::string basenameOf(const std::string &path)
{
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

std::string titleFromPath(const std::string &path)
{
    std::string title = basenameOf(path);
    const size_t dot = title.find_last_of('.');
    if (dot != std::string::npos) {
        title = title.substr(0, dot);
    }
    return title.empty() ? path : title;
}

std::string decodeUrlComponent(const std::string &in)
{
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        const char c = in[i];
        if (c == '%' && i + 2 < in.size()) {
            const int hi = hexNibble(in[i + 1]);
            const int lo = hexNibble(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

std::string stripFragmentAndQuery(const std::string &path)
{
    size_t end = path.find('#');
    const size_t q = path.find('?');
    if (end == std::string::npos || (q != std::string::npos && q < end)) {
        end = q;
    }
    return end == std::string::npos ? path : path.substr(0, end);
}

std::vector<std::string> splitPathParts(const std::string &path)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (start < path.size()) {
        while (start < path.size() && path[start] == '/') {
            start++;
        }
        if (start >= path.size()) {
            break;
        }
        size_t end = path.find('/', start);
        if (end == std::string::npos) {
            end = path.size();
        }
        parts.push_back(path.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

std::string joinPartsAsPath(const std::vector<std::string> &parts, bool absolute)
{
    if (parts.empty()) {
        return absolute ? "/" : "";
    }
    std::string out = absolute ? "/" : "";
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) {
            out.push_back('/');
        }
        out += parts[i];
    }
    return out;
}

std::string resolveRelativePath(const std::string &baseDir, const std::string &reference)
{
    std::string ref = decodeUrlComponent(stripFragmentAndQuery(reference));
    if (ref.empty()) {
        return normalizeDir(baseDir);
    }

    for (char &c : ref) {
        if (c == '\\') {
            c = '/';
        }
    }

    const bool baseIsEpubVirtual = (normalizeDir(baseDir).rfind(EPUB_VIRTUAL_ROOT, 0) == 0);
    bool absolute = !ref.empty() && ref[0] == '/';
    std::vector<std::string> parts;
    if (absolute) {
        if (baseIsEpubVirtual) {
            parts = splitPathParts(EPUB_VIRTUAL_ROOT);
        }
    } else {
        parts = splitPathParts(normalizeDir(baseDir));
    }
    const std::vector<std::string> refParts = splitPathParts(ref);

    for (const std::string &part : refParts) {
        if (part.empty() || part == ".") {
            continue;
        }
        if (part == "..") {
            if (!parts.empty()) {
                parts.pop_back();
            }
            continue;
        }
        parts.push_back(part);
    }

    return normalizeDir(joinPartsAsPath(parts, true));
}

std::string normalizeArchivePath(const std::string &path)
{
    std::string p = path;
    for (char &c : p) {
        if (c == '\\') {
            c = '/';
        }
    }
    while (!p.empty() && p[0] == '.') {
        if (p.size() > 1 && p[1] == '/') {
            p.erase(0, 2);
        } else {
            break;
        }
    }
    while (!p.empty() && p[0] == '/') {
        p.erase(0, 1);
    }
    return p;
}

uint16_t readLe16(const uint8_t *p)
{
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t readLe32(const uint8_t *p)
{
    return static_cast<uint32_t>(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                 (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
}

bool isEpubVirtualPath(const std::string &path)
{
    return normalizeDir(path).rfind(EPUB_VIRTUAL_ROOT, 0) == 0;
}

std::string toEpubVirtualPath(const std::string &entryPath)
{
    const std::string normalized = normalizeArchivePath(entryPath);
    if (normalized.empty()) {
        return EPUB_VIRTUAL_ROOT;
    }
    return std::string(EPUB_VIRTUAL_ROOT) + "/" + normalized;
}

std::string epubVirtualPathToEntryPath(const std::string &virtualPath)
{
    std::string normalized = normalizeDir(virtualPath);
    if (normalized.rfind(EPUB_VIRTUAL_ROOT, 0) != 0) {
        return "";
    }
    normalized.erase(0, std::strlen(EPUB_VIRTUAL_ROOT));
    if (!normalized.empty() && normalized[0] == '/') {
        normalized.erase(0, 1);
    }
    return normalizeArchivePath(normalized);
}

#if INKHUD_BOOKREADER_HAS_SD
bool readFileAt(File &file, uint32_t offset, uint8_t *buffer, size_t length)
{
    if (!buffer || length == 0U) {
        return false;
    }
    concurrency::LockGuard guard(spiLock);
    if (file.position() != offset && !file.seek(offset)) {
        BR_PERF_LOG("archive seek failed: offset=%lu bytes=%u", static_cast<unsigned long>(offset),
                    static_cast<unsigned>(length));
        return false;
    }
    const int32_t n = file.read(buffer, length);
    return n == static_cast<int32_t>(length);
}
#endif

#if INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES && INKHUD_BOOKREADER_USE_JPEGDEC
void epubJpegReadAheadWorker(void *context)
{
    auto *stream = static_cast<EpubJpegStream *>(context);
    File &archive = stream->archive ? *stream->archive : gEpubArchiveFile;
#if INKHUD_BOOKREADER_PERF_LOG
    const uint32_t startMs = Time::getMillis();
#endif
    stream->readAheadSucceeded =
        readFileAt(archive, stream->dataOffset + stream->readAheadOffset, stream->readAheadBuffer.data(), stream->readAheadSize);
#if INKHUD_BOOKREADER_PERF_LOG
    stream->readAheadMs = Time::getMillis() - startMs;
#endif
    // The owner may release the stream immediately after this publication.
    stream->readAheadDone.store(true);
    vTaskDelete(nullptr);
}

void startEpubJpegReadAhead(EpubJpegStream &stream)
{
#if INKHUD_BOOKREADER_GRAYSCALE
    const uint32_t nextOffset = stream.bufferedOffset + stream.bufferedSize;
    if (!stream.readAheadEnabled || nextOffset >= stream.dataSize)
        return;
    if (stream.readAheadBuffer.empty()) {
        if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) < stream.buffer.size() + 131072U ||
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 8192U) {
            stream.readAheadEnabled = false;
            return;
        }
        stream.readAheadBuffer.resize(stream.buffer.size());
    }
    stream.readAheadOffset = nextOffset;
    stream.readAheadSize = std::min<size_t>(stream.buffer.size(), stream.dataSize - nextOffset);
    stream.readAheadSucceeded = false;
    stream.readAheadDone.store(false);
    stream.readAheadActive =
        xTaskCreatePinnedToCore(epubJpegReadAheadWorker, "reader-sd", 4096, &stream, 1, nullptr, xPortGetCoreID() ^ 1) == pdPASS;
    if (!stream.readAheadActive) {
        stream.readAheadSize = 0;
        stream.readAheadEnabled = false;
    }
#endif
}

int32_t epubJpegReadCallback(JPEGFILE *file, uint8_t *buffer, int32_t length)
{
    auto *stream = file ? static_cast<EpubJpegStream *>(file->fHandle) : nullptr;
    if (!stream || !buffer || length <= 0 || file->iPos < 0 || static_cast<uint32_t>(file->iPos) >= stream->dataSize)
        return 0;
    const size_t readLength = std::min<size_t>(static_cast<size_t>(length), stream->dataSize - file->iPos);
    if (stream->buffer.empty())
        stream->buffer.resize(std::min<size_t>(65536U, stream->dataSize));
    size_t copied = 0;
    while (copied < readLength) {
        const uint32_t position = static_cast<uint32_t>(file->iPos);
        if (position < stream->bufferedOffset || position - stream->bufferedOffset >= stream->bufferedSize) {
            stream->finishReadAhead();
            stream->bufferedSize = 0;
            stream->bufferedOffset = position;
            const size_t refillSize = std::min<size_t>(stream->buffer.size(), stream->dataSize - position);
            if (stream->readAheadSize && stream->readAheadOffset == position && stream->readAheadSucceeded) {
                stream->buffer.swap(stream->readAheadBuffer);
                stream->bufferedSize = stream->readAheadSize;
#if INKHUD_BOOKREADER_PERF_LOG
                ++stream->readAheadHits;
#endif
            } else {
                File &archive = stream->archive ? *stream->archive : gEpubArchiveFile;
#if INKHUD_BOOKREADER_PERF_LOG
                const uint32_t readStartMs = Time::getMillis();
#endif
                const bool readOk = readFileAt(archive, stream->dataOffset + position, stream->buffer.data(), refillSize);
#if INKHUD_BOOKREADER_PERF_LOG
                stream->readMs += Time::getMillis() - readStartMs;
                ++stream->readCalls;
#endif
                if (!readOk)
                    break;
                stream->bufferedSize = refillSize;
            }
            stream->readAheadSize = 0;
            startEpubJpegReadAhead(*stream);
        }
        const size_t offset = position - stream->bufferedOffset;
        const size_t count = std::min(readLength - copied, stream->bufferedSize - offset);
        std::memcpy(buffer + copied, stream->buffer.data() + offset, count);
        copied += count;
        file->iPos += static_cast<int32_t>(count);
    }
    return static_cast<int32_t>(copied);
}

int32_t epubJpegSeekCallback(JPEGFILE *file, int32_t position)
{
    auto *stream = file ? static_cast<EpubJpegStream *>(file->fHandle) : nullptr;
    if (!stream)
        return 0;
    file->iPos = std::clamp<int32_t>(position, 0, static_cast<int32_t>(stream->dataSize));
    return file->iPos;
}

void epubJpegCloseCallback(void *) {}
#endif

const ZipCentralEntry *findZipEntryByKey(const std::vector<ZipCentralEntry> &entries, const std::string &pathOrKey)
{
    const std::string exact = normalizeArchivePath(pathOrKey);
    const std::string key = toLowerCopy(exact);
    if (key.empty()) {
        return nullptr;
    }
    const ZipCentralEntry *fallback = nullptr;
    bool ambiguous = false;
    for (const ZipCentralEntry &entry : entries) {
        if (entry.path == exact)
            return &entry;
        if (entry.key == key) {
            ambiguous = fallback != nullptr;
            fallback = &entry;
        }
    }
    return ambiguous ? nullptr : fallback;
}

#if INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES && INKHUD_BOOKREADER_USE_JPEGDEC
bool prepareEpubJpegStream(const std::string &virtualPath, EpubJpegStream &stream)
{
    const ZipCentralEntry *entry = findZipEntryByKey(gEpubArchiveCache.entries, epubVirtualPathToEntryPath(virtualPath));
    if (!entry || entry->method != 0U || entry->compressedSize == 0U || entry->compressedSize > 0x7FFFFFFFU)
        return false;

    std::array<uint8_t, 30U> localHeader{};
    if (!readFileAt(gEpubArchiveFile, entry->localHeaderOffset, localHeader.data(), localHeader.size()) ||
        readLe32(localHeader.data()) != 0x04034B50UL)
        return false;

    const uint16_t nameLen = readLe16(localHeader.data() + 26U);
    const uint16_t extraLen = readLe16(localHeader.data() + 28U);
    stream.dataOffset = entry->localHeaderOffset + 30U + static_cast<uint32_t>(nameLen) + static_cast<uint32_t>(extraLen);
    stream.dataSize = entry->compressedSize;
    BR_PERF_LOG("JPEG stream path=%s offset=%lu bytes=%lu", virtualPath.c_str(), static_cast<unsigned long>(stream.dataOffset),
                static_cast<unsigned long>(stream.dataSize));
    return true;
}

int jpegPrefetchDrawCallback(JPEGDRAW *draw)
{
    auto *job = draw ? static_cast<JpegPrefetchJob *>(draw->pUser) : nullptr;
    if (!job)
        return 0;
    while ((gJpegPrefetchPaused.load() || gReaderDisplayRefreshActive.load()) && !job->cancelled.load())
        vTaskDelay(1);
    if (job->cancelled.load())
        return 0;
    const int width = draw->iWidthUsed > 0 && draw->iWidthUsed <= draw->iWidth ? draw->iWidthUsed : draw->iWidth;
    if (!draw->pPixels || draw->iBpp != 8 ||
        !InkHUD::packGraySourceBlock(reinterpret_cast<const uint8_t *>(draw->pPixels), draw->x, draw->y, width, draw->iHeight,
                                     draw->iWidth, job->width, job->height, job->bitmap)) {
        job->cancelled.store(true);
        return 0;
    }
    if (++job->blocksSinceYield == 16) {
        job->blocksSinceYield = 0;
        vTaskDelay(1);
    }
    return 1;
}

void jpegPrefetchWorker(void *argument)
{
    auto *job = static_cast<JpegPrefetchJob *>(argument);
    if (job->decoder.open(&job->stream, static_cast<int>(job->stream.dataSize), epubJpegCloseCallback, epubJpegReadCallback,
                          epubJpegSeekCallback, jpegPrefetchDrawCallback)) {
        job->decoder.setPixelType(EIGHT_BIT_GRAYSCALE);
        job->decoder.setUserPointer(job);
        if (job->decoder.getWidth() == job->width && job->decoder.getHeight() == job->height)
            job->succeeded =
                job->decoder.decode(0, 0, 0) != 0 && job->decoder.getLastError() == JPEG_SUCCESS && !job->cancelled.load();
        job->decoder.close();
    }
    {
        concurrency::LockGuard guard(spiLock);
        job->archive.close();
    }
    job->decodeMs = Time::getMillis() - job->startedMs;
#if INKHUD_BOOKREADER_PERF_LOG
    BR_PERF_LOG("JPEG prefetch IO path=%s reads=%lu readMs=%lu totalMs=%lu", job->path.c_str(),
                static_cast<unsigned long>(job->stream.readCalls), static_cast<unsigned long>(job->stream.readMs),
                static_cast<unsigned long>(job->decodeMs));
#endif
    job->done.store(true);
    vTaskDelete(nullptr);
}

bool startJpegPrefetch(const std::string &path, const ImageProbeResult &probe, [[maybe_unused]] const std::string &currentText)
{
    static JpegPrefetchSleepGuard sleepGuard;
    if (gJpegPrefetchJob)
        return gJpegPrefetchJob->path == path;
    if (probe.type != ImageFileType::JPEG || !isEpubVirtualPath(path) ||
        monoBitmapBytes(probe.width, probe.height) > SOURCE_IMAGE_MAX_BYTES || !findDecodedImageSlot(path, &currentText))
        return false;
#if INKHUD_BOOKREADER_GRAYSCALE
    const size_t incomingBytes = monoBitmapBytes(probe.width, probe.height);
    // Speculative loading must not evict the source needed for the current page's zoom.
    if (!sourceImagePrefetchFits(incomingBytes, findDecodedImageSlot(path, &currentText), currentText) ||
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) < incomingBytes + 1024U * 1024U) {
        BR_PERF_LOG("JPEG prefetch skipped to retain current sources path=%s bytes=%u", path.c_str(),
                    static_cast<unsigned>(incomingBytes));
        return false;
    }
#endif
    auto *job = new (std::nothrow) JpegPrefetchJob;
    if (!job)
        return false;
    if (!prepareEpubJpegStream(path, job->stream)) {
        delete job;
        return false;
    }
    {
        concurrency::LockGuard guard(spiLock);
        job->archive = SD.open(gEpubArchiveCache.epubPath.c_str(), FILE_READ);
    }
    if (!job->archive) {
        delete job;
        return false;
    }
    job->path = path;
    job->width = probe.width;
    job->height = probe.height;
#if INKHUD_BOOKREADER_GRAYSCALE
    trimSourceImages(incomingBytes, nullptr, &currentText);
#endif
    if (!allocateImageBitmap(job->bitmap, monoBitmapBytes(probe.width, probe.height), nullptr, &currentText)) {
        concurrency::LockGuard guard(spiLock);
        job->archive.close();
        delete job;
        return false;
    }
    job->stream.archive = &job->archive;
    job->startedMs = Time::getMillis();
    // Keep speculative decoding off the rendering core on dual-core boards.
    const BaseType_t renderCore = xPortGetCoreID();
    const BaseType_t workerCore = portNUM_PROCESSORS > 1 ? (renderCore ^ 1) : tskNO_AFFINITY;
    if (xTaskCreatePinnedToCore(jpegPrefetchWorker, "reader-jpeg", 8192, job, 1, nullptr, workerCore) != pdPASS) {
        concurrency::LockGuard guard(spiLock);
        job->archive.close();
        delete job;
        return false;
    }
    gJpegPrefetchJob = job;
    BR_PERF_LOG("JPEG worker started path=%s size=%ux%u core=%d renderCore=%d", path.c_str(), probe.width, probe.height,
                static_cast<int>(workerCore), static_cast<int>(renderCore));
    return true;
}
#endif

#if INKHUD_BOOKREADER_HAS_SD
bool parseZipCentralDirectory(const std::string &zipPath, std::vector<ZipCentralEntry> &entriesOut, File &openFileOut)
{
    entriesOut.clear();

    File file;
    uint32_t fileSize = 0U;
    {
        concurrency::LockGuard guard(spiLock);
        file = SD.open(zipPath.c_str(), FILE_READ);
        if (!file || file.isDirectory()) {
            if (file) {
                file.close();
            }
            return false;
        }
        fileSize = static_cast<uint32_t>(file.size());
    }
    if (fileSize < 22U) {
        concurrency::LockGuard guard(spiLock);
        file.close();
        return false;
    }

    const uint32_t tailWindow = std::min<uint32_t>(fileSize, 65535U + 22U);
    std::vector<uint8_t> tail(std::min<uint32_t>(tailWindow, 4096U));
    int32_t eocdPos = -1;
    while (true) {
        if (!readFileAt(file, fileSize - tail.size(), tail.data(), tail.size())) {
            concurrency::LockGuard guard(spiLock);
            file.close();
            return false;
        }
        for (int32_t i = static_cast<int32_t>(tail.size()) - 22; i >= 0; i--) {
            const uint8_t *candidate = tail.data() + i;
            const uint32_t directorySize = readLe32(candidate + 12U), directoryOffset = readLe32(candidate + 16U);
            const uint32_t footerOffset = fileSize - tail.size() + i;
            if (readLe32(candidate) == 0x06054B50UL && static_cast<size_t>(i) + 22U + readLe16(candidate + 20U) == tail.size() &&
                directorySize && directorySize <= MAX_EPUB_CENTRAL_DIR_BYTES && directoryOffset <= footerOffset &&
                directorySize <= footerOffset - directoryOffset) {
                eocdPos = i;
                break;
            }
        }
        if (eocdPos >= 0 || tail.size() == tailWindow)
            break;
        // Long ZIP comments can put the directory footer outside the small window.
        tail.resize(tailWindow);
    }
    if (eocdPos < 0) {
        concurrency::LockGuard guard(spiLock);
        file.close();
        return false;
    }

    const uint8_t *eocd = &tail[static_cast<size_t>(eocdPos)];
    const uint16_t totalEntries = readLe16(eocd + 10);
    const uint32_t cdSize = readLe32(eocd + 12);
    const uint32_t cdOffset = readLe32(eocd + 16);
    if (cdSize == 0U || cdSize > MAX_EPUB_CENTRAL_DIR_BYTES || cdOffset > fileSize || cdSize > fileSize - cdOffset) {
        concurrency::LockGuard guard(spiLock);
        file.close();
        return false;
    }

    std::vector<uint8_t> cd(cdSize);
    if (!readFileAt(file, cdOffset, cd.data(), cd.size())) {
        concurrency::LockGuard guard(spiLock);
        file.close();
        return false;
    }

    entriesOut.reserve(std::min<size_t>(static_cast<size_t>(totalEntries), 2048U));
    size_t off = 0U;
    while (off + 46U <= cd.size()) {
        const uint8_t *hdr = cd.data() + off;
        if (readLe32(hdr) != 0x02014B50UL) {
            break;
        }

        const uint16_t gpFlags = readLe16(hdr + 8U);
        const uint16_t method = readLe16(hdr + 10U);
        const uint32_t compressedSize = readLe32(hdr + 20U);
        const uint32_t uncompressedSize = readLe32(hdr + 24U);
        const uint16_t fileNameLen = readLe16(hdr + 28U);
        const uint16_t extraLen = readLe16(hdr + 30U);
        const uint16_t commentLen = readLe16(hdr + 32U);
        const uint32_t localHeaderOffset = readLe32(hdr + 42U);

        const size_t nameStart = off + 46U;
        const size_t next =
            nameStart + static_cast<size_t>(fileNameLen) + static_cast<size_t>(extraLen) + static_cast<size_t>(commentLen);
        if (next > cd.size()) {
            break;
        }

        std::string rawName(reinterpret_cast<const char *>(cd.data() + nameStart), static_cast<size_t>(fileNameLen));
        for (char &c : rawName) {
            if (c == '\\') {
                c = '/';
            }
        }
        const bool isDir = !rawName.empty() && rawName.back() == '/';
        const std::string normalizedName = normalizeArchivePath(rawName);
        const bool encrypted = (gpFlags & 0x0001U) != 0U;
        if (!normalizedName.empty() && !isDir && !encrypted) {
            ZipCentralEntry entry;
            entry.path = normalizedName;
            entry.key = toLowerCopy(normalizedName);
            entry.localHeaderOffset = localHeaderOffset;
            entry.compressedSize = compressedSize;
            entry.uncompressedSize = uncompressedSize;
            entry.crc = readLe32(hdr + 16);
            entry.method = method;
            entriesOut.push_back(std::move(entry));
        }

        off = next;
    }

    if (entriesOut.empty()) {
        concurrency::LockGuard guard(spiLock);
        file.close();
        return false;
    }

    openFileOut = file;

    return true;
}

#endif

bool ensureEpubArchiveCached(const std::string &epubPath)
{
    BR_LOAD_SCOPE("archive-index");
    BR_PERF_LOG("archive path=%s cached=%u", epubPath.c_str(),
                static_cast<unsigned>(gEpubArchiveCache.ready && gEpubArchiveCache.epubPath == epubPath));
#if !INKHUD_BOOKREADER_HAS_SD
    (void)epubPath;
    gEpubArchiveCache.clear();
    return false;
#else
    if (gEpubArchiveCache.ready && gEpubArchiveCache.epubPath == epubPath) {
        if (gEpubArchiveFile) {
            return true;
        }
        {
            concurrency::LockGuard guard(spiLock);
            gEpubArchiveFile = SD.open(epubPath.c_str(), FILE_READ);
            if (!gEpubArchiveFile || gEpubArchiveFile.isDirectory()) {
                if (gEpubArchiveFile) {
                    gEpubArchiveFile.close();
                }
                gEpubArchiveCache.clear();
                return false;
            }
        }
        return true;
    }

    closeCachedEpubFile();
    gEpubArchiveCache.clear();

    std::vector<ZipCentralEntry> entries;
    if (!parseZipCentralDirectory(epubPath, entries, gEpubArchiveFile)) {
        closeCachedEpubFile();
        gEpubArchiveCache.clear();
        return false;
    }
    if (!gEpubArchiveFile || gEpubArchiveFile.isDirectory()) {
        closeCachedEpubFile();
        gEpubArchiveCache.clear();
        return false;
    }

    gEpubArchiveCache.epubPath = epubPath;
    gEpubArchiveCache.entries = std::move(entries);
    gEpubArchiveCache.ready = true;
    return true;
#endif
}

bool readEpubEntryLimited(const std::string &entryPath, size_t maxBytes, std::string &out, bool &truncatedOut)
{
    BR_LOAD_SCOPE("epub-text-read");
    out.clear();
    std::vector<uint8_t> bytes;
    if (!readEpubEntryBinaryLimited(entryPath, maxBytes, bytes, truncatedOut))
        return false;
    if (!bytes.empty())
        out.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    return true;
}

bool readEpubVirtualTextFileLimited(const std::string &path, size_t maxBytes, std::string &out, bool *truncatedOut)
{
    bool truncated = false;
    const std::string entryPath = epubVirtualPathToEntryPath(path);
    if (entryPath.empty()) {
        out.clear();
        if (truncatedOut) {
            *truncatedOut = false;
        }
        return false;
    }
    const bool ok = readEpubEntryLimited(entryPath, maxBytes, out, truncated);
    if (truncatedOut) {
        *truncatedOut = truncated;
    }
    return ok;
}

bool readEpubEntryBinaryLimited(const std::string &entryPath, size_t maxBytes, std::vector<uint8_t> &out, bool &truncatedOut)
{
    BR_LOAD_SCOPE("epub-entry-read");
    BR_PERF_LOG("binary entry=%s limit=%u", entryPath.c_str(), static_cast<unsigned>(maxBytes));
#if !INKHUD_BOOKREADER_HAS_SD || !defined(ARCH_ESP32)
    (void)entryPath;
    (void)maxBytes;
    out.clear();
    truncatedOut = false;
    return false;
#else
    out.clear();
    truncatedOut = false;
    if (!gEpubArchiveCache.ready || gEpubArchiveCache.epubPath.empty() || maxBytes == 0U) {
        return false;
    }

    const ZipCentralEntry *entry = findZipEntryByKey(gEpubArchiveCache.entries, entryPath);
    if (!entry) {
        BR_PERF_LOG("readEpubEntryBinaryLimited missing-entry path=%s", entryPath.c_str());
        return false;
    }
    if (!(entry->method == 0U || entry->method == 8U)) {
        BR_PERF_LOG("readEpubEntryBinaryLimited unsupported-method path=%s method=%u", entryPath.c_str(),
                    static_cast<unsigned>(entry->method));
        return false;
    }
    if (entry->compressedSize > MAX_EPUB_COMPRESSED_ENTRY_BYTES || entry->uncompressedSize > MAX_EPUB_UNCOMPRESSED_ENTRY_BYTES) {
        BR_PERF_LOG("readEpubEntryBinaryLimited size-limit path=%s comp=%lu uncomp=%lu", entryPath.c_str(),
                    static_cast<unsigned long>(entry->compressedSize), static_cast<unsigned long>(entry->uncompressedSize));
        truncatedOut = true;
        return false;
    }

    if (!gEpubArchiveFile || gEpubArchiveFile.isDirectory()) {
        closeCachedEpubFile();
        {
            concurrency::LockGuard guard(spiLock);
            gEpubArchiveFile = SD.open(gEpubArchiveCache.epubPath.c_str(), FILE_READ);
            if (!gEpubArchiveFile || gEpubArchiveFile.isDirectory()) {
                if (gEpubArchiveFile) {
                    gEpubArchiveFile.close();
                }
                return false;
            }
        }
    }

    std::array<uint8_t, 30U> localHeader{};
    if (!readFileAt(gEpubArchiveFile, entry->localHeaderOffset, localHeader.data(), localHeader.size())) {
        return false;
    }
    if (readLe32(localHeader.data()) != 0x04034B50UL) {
        BR_PERF_LOG("readEpubEntryBinaryLimited bad-local-header path=%s", entryPath.c_str());
        return false;
    }

    const uint16_t nameLen = readLe16(localHeader.data() + 26U);
    const uint16_t extraLen = readLe16(localHeader.data() + 28U);
    const uint32_t dataOffset = entry->localHeaderOffset + 30U + static_cast<uint32_t>(nameLen) + static_cast<uint32_t>(extraLen);

    if (entry->method == 0U) {
        const size_t readLen = std::min<size_t>(static_cast<size_t>(entry->compressedSize), maxBytes);
        out.resize(readLen);
        if (readLen > 0U && !readFileAt(gEpubArchiveFile, dataOffset, out.data(), out.size())) {
            out.clear();
            return false;
        }
        truncatedOut = (static_cast<size_t>(entry->compressedSize) > maxBytes);
        return true;
    }

    if (entry->uncompressedSize == 0U) {
        return true;
    }

    [[maybe_unused]] const uint32_t readStartMs = millis();
    const size_t outputLimit = std::min<size_t>(entry->uncompressedSize, maxBytes);
    const bool prefixOnly = outputLimit < entry->uncompressedSize;
    std::vector<uint8_t> inflated(outputLimit);
    std::vector<uint8_t> compressed;
    size_t inflatedSize = 0U;
    size_t compressedLimit = prefixOnly ? std::min<size_t>(entry->compressedSize, outputLimit + 4096U) : entry->compressedSize;
    while (true) {
        const size_t previousSize = compressed.size();
        compressed.resize(compressedLimit);
        if (compressedLimit > previousSize && !readFileAt(gEpubArchiveFile, dataOffset + previousSize,
                                                          compressed.data() + previousSize, compressedLimit - previousSize)) {
            return false;
        }
        if (inflateDeflateRawHeapBacked(compressed.data(), compressed.size(), inflated.data(), inflated.size(), inflatedSize,
                                        prefixOnly, compressedLimit < entry->compressedSize) &&
            inflatedSize == outputLimit)
            break;
        if (compressedLimit == entry->compressedSize) {
            BR_PERF_LOG("readEpubEntryBinaryLimited inflate-failed path=%s comp=%lu uncomp=%lu", entryPath.c_str(),
                        static_cast<unsigned long>(entry->compressedSize), static_cast<unsigned long>(entry->uncompressedSize));
            return false;
        }
        compressedLimit = std::min<size_t>(entry->compressedSize, compressedLimit * 2U);
    }
    BR_PERF_LOG("entry SD read entry=%s method=%u compressedRead=%u compressed=%lu uncompressed=%lu ms=%lu", entryPath.c_str(),
                static_cast<unsigned>(entry->method), static_cast<unsigned>(compressed.size()),
                static_cast<unsigned long>(entry->compressedSize), static_cast<unsigned long>(entry->uncompressedSize),
                static_cast<unsigned long>(millis() - readStartMs));

    out = std::move(inflated);
    truncatedOut = prefixOnly;
    return true;
#endif
}

bool readEpubVirtualBinaryFileLimited(const std::string &path, size_t maxBytes, std::vector<uint8_t> &out, bool *truncatedOut)
{
    out.clear();
    bool truncated = false;
    const std::string entryPath = epubVirtualPathToEntryPath(path);
    if (entryPath.empty()) {
        if (truncatedOut) {
            *truncatedOut = false;
        }
        return false;
    }
    const bool ok = readEpubEntryBinaryLimited(entryPath, maxBytes, out, truncated);
    if (truncatedOut) {
        *truncatedOut = truncated;
    }
    return ok;
}

bool inflateDeflateRawHeapBacked(const uint8_t *src, size_t srcLen, uint8_t *dst, size_t dstCap, size_t &outLen, bool prefixOnly,
                                 bool inputHasMore)
{
    BR_LOAD_SCOPE("zip-inflate");
    BR_PERF_LOG("inflate compressed=%u outputLimit=%u prefixOnly=%u", static_cast<unsigned>(srcLen),
                static_cast<unsigned>(dstCap), static_cast<unsigned>(prefixOnly));
#if !INKHUD_BOOKREADER_HAS_SD
    (void)src;
    (void)srcLen;
    (void)dst;
    (void)dstCap;
    (void)prefixOnly;
    (void)inputHasMore;
    outLen = 0U;
    return false;
#else
    outLen = 0U;
    if (!src || !dst || srcLen == 0U || dstCap == 0U) {
        return false;
    }

    tinfl_decompressor *decomp = static_cast<tinfl_decompressor *>(std::malloc(sizeof(tinfl_decompressor)));
    if (!decomp) {
        return false;
    }
    tinfl_init(decomp);

    size_t inOfs = 0U;
    size_t outOfs = 0U;
    while (true) {
        size_t inBytes = srcLen - inOfs;
        size_t outBytes = dstCap - outOfs;
        mz_uint32 flags = TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF;
        if (inputHasMore || inOfs + inBytes < srcLen) {
            flags |= TINFL_FLAG_HAS_MORE_INPUT;
        }

        const tinfl_status status = tinfl_decompress(decomp, src + inOfs, &inBytes, dst, dst + outOfs, &outBytes, flags);
        inOfs += inBytes;
        outOfs += outBytes;

        if (status == TINFL_STATUS_DONE) {
            outLen = outOfs;
            std::free(decomp);
            return true;
        }
        if (status < 0) {
            std::free(decomp);
            return false;
        }

        if (status == TINFL_STATUS_NEEDS_MORE_INPUT && inOfs >= srcLen) {
            std::free(decomp);
            return false;
        }
        if (status == TINFL_STATUS_HAS_MORE_OUTPUT && outOfs >= dstCap) {
            outLen = outOfs;
            std::free(decomp);
            return prefixOnly;
        }
        if (inBytes == 0U && outBytes == 0U) {
            std::free(decomp);
            return false;
        }
    }
#endif
}

bool parseEpubRootfilePath(const std::string &containerXml, std::string &opfPathOut)
{
    opfPathOut.clear();
    if (containerXml.empty()) {
        return false;
    }

    const std::string lower = toLowerCopy(containerXml);
    size_t pos = 0;
    while (true) {
        const size_t rootfilePos = lower.find("<rootfile", pos);
        if (rootfilePos == std::string::npos) {
            break;
        }
        const size_t tagEnd = InkHUD::readerTagEnd(lower, rootfilePos);
        if (tagEnd == std::string::npos) {
            break;
        }
        const std::string tagOriginal = containerXml.substr(rootfilePos + 1U, tagEnd - rootfilePos - 1U);
        std::string fullPath = InkHUD::readerXmlAttribute(tagOriginal, "full-path");
        if (!fullPath.empty()) {
            fullPath = normalizeArchivePath(decodeUrlComponent(stripFragmentAndQuery(fullPath)));
            if (!fullPath.empty()) {
                opfPathOut = fullPath;
                return true;
            }
        }
        pos = tagEnd + 1U;
    }
    return false;
}

void parseOpfTocCandidates(const std::string &opfText, const std::string &opfDir, std::vector<std::string> &tocPathsOut)
{
    tocPathsOut.clear();
    const std::string lower = toLowerCopy(opfText);
    size_t pos = 0;
    while (true) {
        const size_t itemPos = lower.find("<item", pos);
        if (itemPos == std::string::npos) {
            break;
        }
        const size_t tagEnd = InkHUD::readerTagEnd(lower, itemPos);
        if (tagEnd == std::string::npos) {
            break;
        }

        const std::string tagOriginal = opfText.substr(itemPos + 1U, tagEnd - itemPos - 1U);
        const std::string href = InkHUD::readerXmlAttribute(tagOriginal, "href");
        const std::string mediaType = toLowerCopy(InkHUD::readerXmlAttribute(tagOriginal, "media-type"));
        const std::string properties = toLowerCopy(InkHUD::readerXmlAttribute(tagOriginal, "properties"));
        if (!href.empty()) {
            const std::string resolved = resolveRelativePath(opfDir, href);
            const std::string resolvedKey = normalizeArchivePath(resolved);
            if (!resolvedKey.empty()) {
                const std::string lowerPath = toLowerCopy(resolvedKey);
                if (mediaType.find("ncx") != std::string::npos || properties.find("nav") != std::string::npos ||
                    lowerPath.find("toc") != std::string::npos) {
                    tocPathsOut.push_back("/" + resolvedKey);
                }
            }
        }
        pos = tagEnd + 1U;
    }
}

std::string pickFallbackOpfFromZip(const std::vector<ZipCentralEntry> &entries)
{
    std::string firstOpf;
    for (const ZipCentralEntry &entry : entries) {
        if (entry.key.size() >= 4U && entry.key.rfind(".opf") == entry.key.size() - 4U) {
            if (entry.key.find("content.opf") != std::string::npos) {
                return entry.path;
            }
            if (firstOpf.empty()) {
                firstOpf = entry.path;
            }
        }
    }
    return firstOpf;
}

bool readFilePrefix(const std::string &path, size_t maxBytes, std::vector<uint8_t> &out)
{
    out.clear();
    if (!maxBytes || !isEpubVirtualPath(path))
        return false;
    bool truncated = false;
    return readEpubVirtualBinaryFileLimited(path, maxBytes, out, &truncated);
}
ImageProbeResult probePngFromBuffer(const std::vector<uint8_t> &bytes)
{
    ImageProbeResult result;
    if (bytes.size() < 24U) {
        return result;
    }
    static const uint8_t sig[] = {0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
    if (std::memcmp(bytes.data(), sig, sizeof(sig)) != 0) {
        return result;
    }
    const uint16_t widthHi = static_cast<uint16_t>((static_cast<uint16_t>(bytes[16]) << 8) | bytes[17]);
    const uint16_t widthLo = static_cast<uint16_t>((static_cast<uint16_t>(bytes[18]) << 8) | bytes[19]);
    const uint16_t heightHi = static_cast<uint16_t>((static_cast<uint16_t>(bytes[20]) << 8) | bytes[21]);
    const uint16_t heightLo = static_cast<uint16_t>((static_cast<uint16_t>(bytes[22]) << 8) | bytes[23]);
    const uint32_t width = (static_cast<uint32_t>(widthHi) << 16) | widthLo;
    const uint32_t height = (static_cast<uint32_t>(heightHi) << 16) | heightLo;
    if (width == 0U || height == 0U || width > 65535U || height > 65535U) {
        return result;
    }
    result.type = ImageFileType::PNG;
    result.width = static_cast<uint16_t>(width);
    result.height = static_cast<uint16_t>(height);
    result.valid = true;
    return result;
}

ImageProbeResult probeJpegFromBuffer(const std::vector<uint8_t> &bytes, size_t *nextPrefix = nullptr)
{
    ImageProbeResult result;
    if (bytes.size() < 4U || bytes[0] != 0xFFU || bytes[1] != 0xD8U) {
        return result;
    }

    size_t i = 2;
    while (i + 8 < bytes.size()) {
        if (bytes[i] != 0xFFU) {
            i++;
            continue;
        }
        while (i < bytes.size() && bytes[i] == 0xFFU) {
            i++;
        }
        if (i >= bytes.size()) {
            break;
        }
        const uint8_t marker = bytes[i++];
        if (marker == 0xD9U || marker == 0xDAU) {
            break;
        }
        if (i + 1 >= bytes.size()) {
            break;
        }
        const uint16_t segLen = static_cast<uint16_t>((static_cast<uint16_t>(bytes[i]) << 8) | bytes[i + 1]);
        if (segLen < 2U)
            break;
        if (i + segLen > bytes.size()) {
            if (nextPrefix)
                *nextPrefix = i + segLen + 32U;
            break;
        }

        const bool isSof = (marker == 0xC0U || marker == 0xC1U || marker == 0xC2U || marker == 0xC3U || marker == 0xC5U ||
                            marker == 0xC6U || marker == 0xC7U || marker == 0xC9U || marker == 0xCAU || marker == 0xCBU ||
                            marker == 0xCDU || marker == 0xCEU || marker == 0xCFU);
        if (isSof && segLen >= 7U) {
            const uint16_t height = static_cast<uint16_t>((static_cast<uint16_t>(bytes[i + 3]) << 8) | bytes[i + 4]);
            const uint16_t width = static_cast<uint16_t>((static_cast<uint16_t>(bytes[i + 5]) << 8) | bytes[i + 6]);
            if (width > 0U && height > 0U) {
                result.type = ImageFileType::JPEG;
                result.width = width;
                result.height = height;
                result.valid = true;
                return result;
            }
        }

        i += segLen;
    }

    return result;
}

ImageProbeResult probeImageFile(const std::string &path)
{
    const auto cached = std::find_if(gImageProbeCache.begin(), gImageProbeCache.end(),
                                     [&](const ImageProbeCache &entry) { return entry.path == path; });
    if (cached != gImageProbeCache.end())
        return cached->result;
    ImageProbeResult result;
    std::vector<uint8_t> header;
    size_t nextPrefix = 0;
    if (hasRenderableImageExtension(path) && readFilePrefix(path, 4096U, header)) {
        result = probePngFromBuffer(header);
        if (!result.valid)
            result = probeJpegFromBuffer(header, &nextPrefix);
        // JPEG metadata may place the size marker beyond the usual header prefix.
        size_t limit = header.size();
        while (!result.valid && header.size() == limit && limit < MAX_EPUB_INLINE_IMAGE_BYTES && header.size() >= 2U &&
               header[0] == 0xFFU && header[1] == 0xD8U) {
            limit = std::min<size_t>(std::max<size_t>({32768U, limit * 2U, nextPrefix}), MAX_EPUB_INLINE_IMAGE_BYTES);
            if (!readFilePrefix(path, limit, header))
                return result;
            nextPrefix = 0;
            result = probeJpegFromBuffer(header, &nextPrefix);
        }
    }
    BR_PERF_LOG("image probe path=%s headerBytes=%u signature=%02x %02x %02x "
                "%02x valid=%u type=%u size=%ux%u",
                path.c_str(), static_cast<unsigned>(header.size()), header.size() > 0 ? header[0] : 0,
                header.size() > 1 ? header[1] : 0, header.size() > 2 ? header[2] : 0, header.size() > 3 ? header[3] : 0,
                result.valid, static_cast<unsigned>(result.type), result.width, result.height);
    if (gImageProbeCache.size() >= 512U)
        gImageProbeCache.erase(gImageProbeCache.begin());
    gImageProbeCache.push_back({path, result});
    return result;
}

bool warmNextImageHeader(const std::string &text, size_t &offset)
{
    while (offset < text.size()) {
        const size_t end = text.find('\n', offset);
        const std::string line = text.substr(offset, end - offset);
        offset = end == std::string::npos ? text.size() : end + 1;
        size_t start = 0;
        consumeLeadingLineXOffsetMarkers(line, start);
        std::string image;
        std::vector<std::string> images;
        if (!isImageMarkerLine(line.substr(start), &image) &&
            !(isImageSequenceMarkerLine(line.substr(start), &images) && !images.empty()))
            continue;
        probeImageFile(image.empty() ? images.front() : image);
        return true;
    }
    return false;
}

#if INKHUD_BOOKREADER_HAS_SD && INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES
uint16_t scaledDimensionForPowerOfTwo(uint16_t dim, uint8_t scalePower)
{
    if (dim == 0U) {
        return 1U;
    }
    const uint32_t divisor = 1U << std::min<uint8_t>(scalePower, 3U);
    const uint32_t rounded = (static_cast<uint32_t>(dim) + divisor - 1U) / divisor;
    return std::max<uint16_t>(1U, static_cast<uint16_t>(rounded));
}

uint8_t pickJpegScalePower(uint16_t srcWidth, uint16_t srcHeight, uint16_t outWidth, uint16_t outHeight,
                           uint16_t minTargetDivisor)
{
    uint8_t scalePower = 0U;
    const uint16_t divisor = std::max<uint16_t>(1U, minTargetDivisor);
    const uint16_t minDecodedW = std::max<uint16_t>(1U, outWidth / divisor);
    const uint16_t minDecodedH = std::max<uint16_t>(1U, outHeight / divisor);

    while (scalePower < 3U) {
        const uint8_t candidate = static_cast<uint8_t>(scalePower + 1U);
        const uint16_t scaledW = scaledDimensionForPowerOfTwo(srcWidth, candidate);
        const uint16_t scaledH = scaledDimensionForPowerOfTwo(srcHeight, candidate);
        if (scaledW < minDecodedW || scaledH < minDecodedH) {
            break;
        }
        scalePower = candidate;
    }
    return scalePower;
}

UINT jpegRomInputCallback(JDEC *dec, BYTE *buffer, UINT ndata)
{
    if (!dec || ndata == 0U) {
        return 0U;
    }

    JpegRenderContext *ctx = reinterpret_cast<JpegRenderContext *>(dec->device);
    if (!ctx) {
        return 0U;
    }
    if (ctx->memory && ctx->memorySize > 0U) {
        if (ctx->memoryOffset >= ctx->memorySize) {
            return 0U;
        }
        const size_t remaining = ctx->memorySize - ctx->memoryOffset;
        const size_t toCopy = std::min<size_t>(remaining, static_cast<size_t>(ndata));
        if (toCopy == 0U) {
            return 0U;
        }
        if (buffer) {
            std::memcpy(buffer, ctx->memory + ctx->memoryOffset, toCopy);
        }
        ctx->memoryOffset += toCopy;
        return static_cast<uint32_t>(toCopy);
    }
    return 0U;
}

UINT jpegRomOutputCallback(JDEC *dec, void *bitmap, JRECT *rect)
{
    if (!dec || !bitmap || !rect) {
        return 0U;
    }

    JpegRenderContext *ctx = reinterpret_cast<JpegRenderContext *>(dec->device);
    if (!ctx || !ctx->render.applet) {
        return 0U;
    }
    if (interruptImagePrefetch(ctx->render))
        return 0U;

    const int blockW = static_cast<int>(rect->right) - static_cast<int>(rect->left) + 1;
    const int blockH = static_cast<int>(rect->bottom) - static_cast<int>(rect->top) + 1;
    if (blockW <= 0 || blockH <= 0) {
        return 1U;
    }

    const size_t pixelCount = static_cast<size_t>(blockW) * static_cast<size_t>(blockH);
    if (pixelCount > ctx->blockBuffer.size()) {
        return 0U;
    }

    const uint8_t *src = reinterpret_cast<const uint8_t *>(bitmap);
    for (size_t i = 0; i < pixelCount; i++) {
        const uint8_t r = src[i * 3U];
        const uint8_t g = src[i * 3U + 1U];
        const uint8_t b = src[i * 3U + 2U];
        ctx->blockBuffer[i] = static_cast<uint16_t>(((static_cast<uint16_t>(r) & 0xF8U) << 8) |
                                                    ((static_cast<uint16_t>(g) & 0xFCU) << 3) | (static_cast<uint16_t>(b) >> 3));
    }

    drawMonochromeScaledBlock(ctx->render, ctx->blockBuffer.data(), static_cast<int>(rect->left), static_cast<int>(rect->top),
                              blockW, blockH);
    return 1U;
}

bool decodeRomJpeg(const std::vector<uint8_t> &bytes, const ImageRenderContext &render, uint8_t scalePower)
{
    if (bytes.empty())
        return false;
    static JpegRenderContext ctx;
    alignas(4) static std::array<uint8_t, 4096U> workBuffer;
    ctx = {};
    ctx.memory = bytes.data();
    ctx.memorySize = bytes.size();
    ctx.render = render;
    JDEC dec = {};
    const JRESULT prep = jd_prepare(&dec, jpegRomInputCallback, workBuffer.data(), workBuffer.size(), &ctx);
    const JRESULT rc = prep == JDR_OK ? jd_decomp(&dec, jpegRomOutputCallback, scalePower) : prep;
    BR_PERF_LOG("ROM JPEG result=%d size=%u", static_cast<int>(rc), static_cast<unsigned>(bytes.size()));
    return rc == JDR_OK;
}

void drawMonochromeScaledBlockStride(ImageRenderContext &ctx, const uint16_t *pixels, int srcXStart, int srcYStart, int srcBlockW,
                                     int srcBlockH, int srcStride)
{
    if (!ctx.applet || !pixels || srcBlockW <= 0 || srcBlockH <= 0 || srcStride < srcBlockW || ctx.srcWidth == 0 ||
        ctx.srcHeight == 0 || ctx.outWidth == 0 || ctx.outHeight == 0) {
        return;
    }
    if (srcXStart >= ctx.srcWidth || srcYStart >= ctx.srcHeight) {
        return;
    }
#if INKHUD_BOOKREADER_GRAYSCALE
    if (ctx.sourceBitmap) {
        InkHUD::rasterizeInlineRgb565Block(pixels, srcXStart, srcYStart, srcBlockW, srcBlockH, srcStride, ctx.srcWidth,
                                           ctx.srcHeight, ctx.srcWidth, ctx.srcHeight, *ctx.sourceBitmap);
        return;
    }
    if (ctx.monoBitmap) {
        InkHUD::rasterizeInlineRgb565Block(pixels, srcXStart, srcYStart, srcBlockW, srcBlockH, srcStride, ctx.srcWidth,
                                           ctx.srcHeight, ctx.outWidth, ctx.outHeight, *ctx.monoBitmap);
        return;
    }
#endif

    if (ctx.inlineRaster && ctx.monoBitmap) {
        InkHUD::rasterizeInlineRgb565Block(pixels, srcXStart, srcYStart, srcBlockW, srcBlockH, srcStride, ctx.srcWidth,
                                           ctx.srcHeight, ctx.outWidth, ctx.outHeight, *ctx.monoBitmap);
        return;
    }

    if (ctx.sourceBitmap) {
        const size_t stride = (static_cast<size_t>(ctx.srcWidth) + 7U) / 8U;
        for (int y = 0; y < srcBlockH && srcYStart + y < ctx.srcHeight; ++y) {
            if (srcYStart + y < 0)
                continue;
            uint8_t *row = ctx.sourceBitmap->data() + (srcYStart + y) * stride;
            for (int x = 0; x < srcBlockW && srcXStart + x < ctx.srcWidth; ++x) {
                const int sx = srcXStart + x;
                if (sx >= 0 && toMonoColor(pixels[y * srcStride + x]) == InkHUD::BLACK)
                    row[sx / 8] |= 0x80U >> (sx & 7);
            }
        }
        return;
    }

    for (int sy = 0; sy < srcBlockH;) {
        const int srcY = srcYStart + sy;
        if (srcY < 0 || srcY >= ctx.srcHeight) {
            sy++;
            continue;
        }

        const int dstY0 = static_cast<int>((static_cast<int32_t>(srcY) * static_cast<int32_t>(ctx.outHeight)) / ctx.srcHeight);
        int dstY1 = static_cast<int>((static_cast<int32_t>(srcY + 1) * static_cast<int32_t>(ctx.outHeight)) / ctx.srcHeight) - 1;
        if (dstY1 < dstY0) {
            dstY1 = dstY0;
        }

        const int nextSrcYGlobal = static_cast<int>(
            (static_cast<int64_t>(dstY1 + 1) * static_cast<int64_t>(ctx.srcHeight) + static_cast<int64_t>(ctx.outHeight) - 1) /
            static_cast<int64_t>(ctx.outHeight));
        const int yStep = std::max(1, std::min(srcBlockH - sy, nextSrcYGlobal - srcY));

        for (int sx = 0; sx < srcBlockW;) {
            const int srcX = srcXStart + sx;
            if (srcX < 0 || srcX >= ctx.srcWidth) {
                sx++;
                continue;
            }

            const int dstX0 = static_cast<int>((static_cast<int32_t>(srcX) * static_cast<int32_t>(ctx.outWidth)) / ctx.srcWidth);
            int dstX1 =
                static_cast<int>((static_cast<int32_t>(srcX + 1) * static_cast<int32_t>(ctx.outWidth)) / ctx.srcWidth) - 1;
            if (dstX1 < dstX0) {
                dstX1 = dstX0;
            }

            const int w = dstX1 - dstX0 + 1;
            const int h = dstY1 - dstY0 + 1;
            if (w <= 0 || h <= 0) {
                sx++;
                continue;
            }

            const uint16_t monoColor = toMonoColor(pixels[sy * srcStride + sx]);
            if (!ctx.skipScreenWrite) {
                ctx.applet->fillRect(static_cast<int16_t>(ctx.left + dstX0), static_cast<int16_t>(ctx.top + dstY0),
                                     static_cast<int16_t>(w), static_cast<int16_t>(h), monoColor);
            }
            if (ctx.monoBitmap && !ctx.monoBitmap->empty() && ctx.outWidth > 0U && ctx.outHeight > 0U) {
                const size_t stride = (static_cast<size_t>(ctx.outWidth) + 7U) / 8U;
                for (int dy = dstY0; dy <= dstY1; dy++) {
                    if (dy < 0 || dy >= ctx.outHeight) {
                        continue;
                    }
                    for (int dx = dstX0; dx <= dstX1; dx++) {
                        if (dx < 0 || dx >= ctx.outWidth) {
                            continue;
                        }
                        const size_t byteIndex = static_cast<size_t>(dy) * stride + (static_cast<size_t>(dx) / 8U);
                        const uint8_t bitMask = static_cast<uint8_t>(0x80U >> (dx & 0x7));
                        if (monoColor == NicheGraphics::InkHUD::BLACK) {
                            (*ctx.monoBitmap)[byteIndex] |= bitMask;
                        } else {
                            (*ctx.monoBitmap)[byteIndex] &= static_cast<uint8_t>(~bitMask);
                        }
                    }
                }
            }

            const int nextSrcXGlobal = static_cast<int>(
                (static_cast<int64_t>(dstX1 + 1) * static_cast<int64_t>(ctx.srcWidth) + static_cast<int64_t>(ctx.outWidth) - 1) /
                static_cast<int64_t>(ctx.outWidth));
            const int xStep = std::max(1, std::min(srcBlockW - sx, nextSrcXGlobal - srcX));
            sx += xStep;
        }
        sy += yStep;
    }
}

void drawMonochromeScaledBlock(ImageRenderContext &ctx, const uint16_t *pixels, int srcXStart, int srcYStart, int srcBlockW,
                               int srcBlockH)
{
    drawMonochromeScaledBlockStride(ctx, pixels, srcXStart, srcYStart, srcBlockW, srcBlockH, srcBlockW);
}

#if INKHUD_BOOKREADER_USE_JPEGDEC
int jpegDecDrawCallback(JPEGDRAW *draw)
{
    if (!draw || !draw->pUser || !draw->pPixels) {
        return 0;
    }

    noteBookReaderLoadActivity();
    JpegDecRenderContext &ctx = *reinterpret_cast<JpegDecRenderContext *>(draw->pUser);
    if (interruptImagePrefetch(ctx))
        return 0;
    const int drawWidth = (draw->iWidthUsed > 0 && draw->iWidthUsed <= draw->iWidth) ? draw->iWidthUsed : draw->iWidth;
    if (drawWidth <= 0 || draw->iHeight <= 0) {
        return 1;
    }

    if (ctx.sourceBitmap && draw->iBpp == 8)
        return InkHUD::packGraySourceBlock(reinterpret_cast<const uint8_t *>(draw->pPixels), draw->x, draw->y, drawWidth,
                                           draw->iHeight, draw->iWidth, ctx.srcWidth, ctx.srcHeight, *ctx.sourceBitmap);

    drawMonochromeScaledBlockStride(ctx, draw->pPixels, draw->x, draw->y, drawWidth, draw->iHeight, draw->iWidth);
    return 1;
}
#endif

int pngDrawCallback(PNGDRAW *draw)
{
    if (!draw || !draw->pUser) {
        return 0;
    }
    noteBookReaderLoadActivity();
    PngRenderContext &ctx = *reinterpret_cast<PngRenderContext *>(draw->pUser);
    if (interruptImagePrefetch(ctx))
        return 0;
    if (!ctx.decoder) {
        return 0;
    }
    if (ctx.lineBuffer.size() < static_cast<size_t>(draw->iWidth)) {
        ctx.lineBuffer.resize(static_cast<size_t>(draw->iWidth));
    }
    ctx.decoder->getLineAsRGB565(draw, ctx.lineBuffer.data(), PNG_RGB565_LITTLE_ENDIAN, 0xFFFFFFFF);
    drawMonochromeScaledBlock(ctx, ctx.lineBuffer.data(), 0, draw->y, draw->iWidth, 1);
    return 1;
}
#endif

std::string decodeXmlEntities(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '&') {
            const size_t end = text.find(';', i + 1);
            if (end != std::string::npos && end - i <= 10) {
                const std::string entity = text.substr(i + 1, end - i - 1);
                const std::string decoded = decodeHtmlEntity(entity);
                if (!decoded.empty()) {
                    out += decoded;
                    i = end + 1;
                    continue;
                }
            }
        }
        out.push_back(text[i]);
        i++;
    }
    return out;
}

std::string stripTagsSimple(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t pos = 0; pos < text.size();) {
        if (text[pos] == '<') {
            const size_t end = InkHUD::readerTagEnd(text, pos + 1);
            if (end == std::string::npos)
                break;
            pos = end + 1;
        } else
            out.push_back(text[pos++]);
    }
    return compactTextForDisplay(decodeXmlEntities(out));
}

std::string leafNameFromReference(const std::string &value)
{
    const std::string path = stripFragmentAndQuery(decodeUrlComponent(value));
    if (path.empty()) {
        return "";
    }
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

bool isTagNameChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
}

bool parseCssLengthToPx(const std::string &css, const char *property, uint16_t &pixels, bool allowZero = false)
{
    pixels = 0;
    const std::string prop(property);
    size_t searchFrom = 0;
    while (searchFrom < css.size()) {
        const size_t pos = css.find(prop, searchFrom);
        if (pos == std::string::npos) {
            return false;
        }
        const bool leftOk = (pos == 0) || !isTagNameChar(css[pos - 1]);
        const size_t rightPos = pos + prop.size();
        const bool rightOk = (rightPos >= css.size()) || !isTagNameChar(css[rightPos]);
        if (!leftOk || !rightOk) {
            searchFrom = pos + 1;
            continue;
        }

        size_t colon = css.find(':', rightPos);
        if (colon == std::string::npos) {
            return false;
        }
        size_t i = colon + 1;
        while (i < css.size() && std::isspace(static_cast<unsigned char>(css[i]))) {
            i++;
        }
        bool negative = false;
        if (i < css.size() && css[i] == '-') {
            negative = true;
            i++;
        }

        size_t numStart = i;
        bool seenDigit = false;
        while (i < css.size() && (std::isdigit(static_cast<unsigned char>(css[i])) || css[i] == '.')) {
            seenDigit = true;
            i++;
        }
        if (!seenDigit) {
            searchFrom = pos + 1;
            continue;
        }

        float value = std::strtof(css.substr(numStart, i - numStart).c_str(), nullptr);
        if (negative || value < 0.0f || (value == 0.0f && !allowZero)) {
            searchFrom = pos + 1;
            continue;
        }

        while (i < css.size() && std::isspace(static_cast<unsigned char>(css[i]))) {
            i++;
        }
        size_t unitStart = i;
        while (i < css.size() && std::isalpha(static_cast<unsigned char>(css[i]))) {
            i++;
        }
        const std::string unit = css.substr(unitStart, i - unitStart);
        float multiplier = 1.0f; // px/default
        if (unit == "em" || unit == "rem") {
            multiplier = 16.0f;
        } else if (unit == "pt") {
            multiplier = 1.33f;
        } else if (unit == "pc") {
            multiplier = 16.0f;
        } else if (unit.empty() || unit == "px") {
            multiplier = 1.0f;
        } else {
            searchFrom = pos + 1;
            continue;
        }

        uint32_t px = static_cast<uint32_t>(value * multiplier);
        px = std::min<uint32_t>(px, STYLE_MAX_INDENT_PX);
        if (px == 0U && !allowZero) {
            searchFrom = pos + 1;
            continue;
        }
        pixels = static_cast<uint16_t>(px);
        return true;
    }
    return false;
}

bool cssWantsBold(const std::string &styleLower)
{
    const size_t prop = styleLower.find("font-weight");
    if (prop == std::string::npos) {
        return false;
    }
    const size_t colon = styleLower.find(':', prop);
    if (colon == std::string::npos) {
        return false;
    }
    size_t end = styleLower.find(';', colon + 1);
    if (end == std::string::npos) {
        end = styleLower.size();
    }
    const std::string value = styleLower.substr(colon + 1, end - (colon + 1));
    if (value.find("bold") != std::string::npos) {
        return true;
    }
    for (size_t i = 0; i < value.size(); i++) {
        if (std::isdigit(static_cast<unsigned char>(value[i]))) {
            const int n = value[i] - '0';
            if (n >= 6) {
                return true;
            }
        }
    }
    return false;
}

bool cssWantsUnderline(const std::string &styleLower)
{
    const size_t deco = styleLower.find("text-decoration");
    if (deco == std::string::npos) {
        return false;
    }
    size_t end = styleLower.find(';', deco);
    if (end == std::string::npos) {
        end = styleLower.size();
    }
    return styleLower.substr(deco, end - deco).find("underline") != std::string::npos;
}

std::string cssPropertyValue(const std::string &css, const char *property);
void splitClassTokens(const std::string &classesLower, std::vector<std::string> &out);

struct CssIndent {
    int16_t margin = -1, padding = -1;
    void apply(const CssIndent &other)
    {
        if (other.margin >= 0)
            margin = other.margin;
        if (other.padding >= 0)
            padding = other.padding;
    }
    uint16_t pixels() const { return std::max<int16_t>({0, margin, padding}); }
};

CssIndent cssIndentPixels(const std::string &styleLower)
{
    CssIndent indent;
    uint16_t parsed = 0;
    std::vector<std::string> margins;
    splitClassTokens(cssPropertyValue(styleLower, "margin"), margins);
    if (!margins.empty() && margins.size() <= 4) {
        const size_t left = margins.size() == 1 ? 0 : margins.size() == 4 ? 3 : 1;
        if (parseCssLengthToPx("margin-left:" + margins[left], "margin-left", parsed, true))
            indent.margin = std::min<uint16_t>(parsed, STYLE_MAX_INDENT_PX);
    }
    if (parseCssLengthToPx(styleLower, "margin-left", parsed, true))
        indent.margin = std::min<uint16_t>(parsed, STYLE_MAX_INDENT_PX);
    if (parseCssLengthToPx(styleLower, "padding-left", parsed, true))
        indent.padding = std::min<uint16_t>(parsed, STYLE_MAX_INDENT_PX);
    return indent;
}

bool cssWantsStrike(const std::string &styleLower)
{
    const size_t deco = styleLower.find("text-decoration");
    if (deco == std::string::npos) {
        return false;
    }
    size_t end = styleLower.find(';', deco);
    if (end == std::string::npos) {
        end = styleLower.size();
    }
    return styleLower.substr(deco, end - deco).find("line-through") != std::string::npos;
}

std::string cssPropertyValue(const std::string &css, const char *property)
{
    std::string value;
    size_t start = 0;
    while (start < css.size()) {
        const size_t end = css.find(';', start);
        const size_t colon = css.find(':', start);
        if (colon != std::string::npos && (end == std::string::npos || colon < end)) {
            auto trim = [](const std::string &text) {
                const size_t first = text.find_first_not_of(" \t\r\n");
                const size_t last = text.find_last_not_of(" \t\r\n");
                return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
            };
            if (trim(css.substr(start, colon - start)) == property)
                value = trim(css.substr(colon + 1, end - colon - 1));
        }
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return value;
}

uint8_t cssAlignment(const std::string &css)
{
    const std::string value = cssPropertyValue(css, "text-align");
    return value == "center" ? 2 : value == "right" || value == "end" ? 3 : value.empty() ? 0 : 1;
}

uint8_t cssParagraphSpacing(const std::string &css)
{
    std::vector<std::string> margins;
    splitClassTokens(cssPropertyValue(css, "margin"), margins);
    uint16_t top = 0, bottom = 0;
    bool hasTop = false, hasBottom = false;
    if (!margins.empty() && margins.size() <= 4) {
        hasTop = parseCssLengthToPx("margin-top:" + margins.front(), "margin-top", top, true);
        hasBottom = parseCssLengthToPx("margin-bottom:" + margins[margins.size() > 2 ? 2 : 0], "margin-bottom", bottom, true);
    }
    uint16_t value = 0;
    if (parseCssLengthToPx(css, "margin-top", value, true)) {
        top = value;
        hasTop = true;
    }
    if (parseCssLengthToPx(css, "margin-bottom", value, true)) {
        bottom = value;
        hasBottom = true;
    }
    return hasTop && hasBottom ? (std::max(top, bottom) < STYLE_INDENT_STEP_PX / 2 ? 1 : 2) : 0;
}

int16_t cssFirstLineIndent(const std::string &css)
{
    uint16_t pixels = 0;
    return parseCssLengthToPx(css, "text-indent", pixels, true) ? static_cast<int16_t>(pixels) : -1;
}

struct CssStyleRule {
    std::string tag;
    std::string id;
    std::vector<std::string> classes;
    bool bold = false;
    bool underline = false;
    bool strike = false;
    CssIndent indent;
    uint8_t alignment = 0;
    uint8_t paragraphSpacing = 0;
    bool noUnderline = false;
    int16_t firstLineIndent = -1;
};

struct ExternalCssCache {
    std::string path;
    size_t limit = 0;
    size_t sourceBytes = 0;
    std::vector<CssStyleRule> rules;
};
std::vector<ExternalCssCache> gExternalCssCaches;

std::string trimCopy(const std::string &in)
{
    size_t start = 0;
    while (start < in.size() && std::isspace(static_cast<unsigned char>(in[start]))) {
        start++;
    }
    size_t end = in.size();
    while (end > start && std::isspace(static_cast<unsigned char>(in[end - 1]))) {
        end--;
    }
    return in.substr(start, end - start);
}

void splitClassTokens(const std::string &classesLower, std::vector<std::string> &out)
{
    out.clear();
    size_t i = 0;
    while (i < classesLower.size()) {
        while (i < classesLower.size() && std::isspace(static_cast<unsigned char>(classesLower[i]))) {
            i++;
        }
        if (i >= classesLower.size()) {
            break;
        }
        size_t j = i;
        while (j < classesLower.size() && !std::isspace(static_cast<unsigned char>(classesLower[j]))) {
            j++;
        }
        if (j > i) {
            out.push_back(classesLower.substr(i, j - i));
        }
        i = j;
    }
}

bool containsToken(const std::vector<std::string> &tokens, const std::string &value)
{
    for (const std::string &token : tokens) {
        if (token == value) {
            return true;
        }
    }
    return false;
}

std::string stripCssComments(const std::string &cssLower)
{
    std::string out;
    out.reserve(cssLower.size());
    size_t i = 0;
    while (i < cssLower.size()) {
        if (i + 1 < cssLower.size() && cssLower[i] == '/' && cssLower[i + 1] == '*') {
            const size_t end = cssLower.find("*/", i + 2);
            if (end == std::string::npos) {
                break;
            }
            i = end + 2;
            continue;
        }
        out.push_back(cssLower[i]);
        i++;
    }
    return out;
}

bool parseSimpleCssSelector(std::string selectorLower, CssStyleRule &ruleOut)
{
    selectorLower = trimCopy(selectorLower);
    if (selectorLower.empty()) {
        return false;
    }

    const size_t pseudo = selectorLower.find(':');
    if (pseudo != std::string::npos) {
        selectorLower = selectorLower.substr(0, pseudo);
    }
    selectorLower = trimCopy(selectorLower);
    if (selectorLower.empty()) {
        return false;
    }

    if (selectorLower.find(' ') != std::string::npos || selectorLower.find('>') != std::string::npos ||
        selectorLower.find('+') != std::string::npos || selectorLower.find('~') != std::string::npos ||
        selectorLower.find('[') != std::string::npos || selectorLower.find('*') != std::string::npos) {
        return false;
    }

    CssStyleRule parsed;
    size_t i = 0;
    if (i < selectorLower.size() && isTagNameChar(selectorLower[i])) {
        const size_t start = i;
        while (i < selectorLower.size() && isTagNameChar(selectorLower[i])) {
            i++;
        }
        parsed.tag = selectorLower.substr(start, i - start);
    }

    while (i < selectorLower.size()) {
        const char marker = selectorLower[i];
        if (marker != '.' && marker != '#') {
            return false;
        }
        i++;
        const size_t start = i;
        while (i < selectorLower.size() && (isTagNameChar(selectorLower[i]) || selectorLower[i] == '_')) {
            i++;
        }
        if (i == start) {
            return false;
        }
        const std::string token = selectorLower.substr(start, i - start);
        if (marker == '.') {
            parsed.classes.push_back(token);
        } else {
            parsed.id = token;
        }
    }

    if (parsed.tag.empty() && parsed.id.empty() && parsed.classes.empty()) {
        return false;
    }
    ruleOut.tag = std::move(parsed.tag);
    ruleOut.id = std::move(parsed.id);
    ruleOut.classes = std::move(parsed.classes);
    return true;
}

void appendCssRulesFromLowerCss(const std::string &cssLower, std::vector<CssStyleRule> &rulesOut)
{
    const std::string css = stripCssComments(cssLower);
    size_t p = 0;
    while (p < css.size()) {
        const size_t braceOpen = css.find('{', p);
        if (braceOpen == std::string::npos) {
            break;
        }
        const size_t braceClose = css.find('}', braceOpen + 1);
        if (braceClose == std::string::npos) {
            break;
        }

        const std::string selectorText = css.substr(p, braceOpen - p);
        const std::string decl = css.substr(braceOpen + 1, braceClose - braceOpen - 1);

        const bool declBold = cssWantsBold(decl);
        const bool declUnderline = cssWantsUnderline(decl);
        const bool declStrike = cssWantsStrike(decl);
        const CssIndent declIndent = cssIndentPixels(decl);
        const uint8_t declAlignment = cssAlignment(decl);
        const uint8_t declSpacing = cssParagraphSpacing(decl);
        const bool declNoUnderline = cssPropertyValue(decl, "text-decoration") == "none";
        const int16_t declFirstIndent = cssFirstLineIndent(decl);
        if (declBold || declUnderline || declStrike || declIndent.margin >= 0 || declIndent.padding >= 0 || declAlignment ||
            declSpacing || declNoUnderline || declFirstIndent >= 0) {
            size_t selStart = 0;
            while (selStart < selectorText.size()) {
                size_t comma = selectorText.find(',', selStart);
                if (comma == std::string::npos) {
                    comma = selectorText.size();
                }
                CssStyleRule rule;
                if (parseSimpleCssSelector(selectorText.substr(selStart, comma - selStart), rule)) {
                    rule.bold = declBold;
                    rule.underline = declUnderline;
                    rule.strike = declStrike;
                    rule.indent = declIndent;
                    rule.alignment = declAlignment;
                    rule.paragraphSpacing = declSpacing;
                    rule.noUnderline = declNoUnderline;
                    rule.firstLineIndent = declFirstIndent;
                    rulesOut.push_back(std::move(rule));
                    if (rulesOut.size() >= 512) {
                        return;
                    }
                }
                selStart = (comma < selectorText.size()) ? (comma + 1) : selectorText.size();
            }
        }

        p = braceClose + 1;
    }
}

bool appendExternalCssRules(const std::string &path, size_t limit, std::vector<CssStyleRule> &rulesOut, size_t &sourceBytes)
{
    for (const auto &cache : gExternalCssCaches) {
        if (cache.path == path && cache.limit == limit) {
            sourceBytes = cache.sourceBytes;
            const size_t count = std::min(cache.rules.size(), 512U - rulesOut.size());
            rulesOut.insert(rulesOut.end(), cache.rules.begin(), cache.rules.begin() + count);
            return true;
        }
    }
    std::string css;
    if (!readEpubVirtualTextFileLimited(path, limit, css, nullptr))
        return false;
    ExternalCssCache cache;
    cache.path = path;
    cache.limit = limit;
    cache.sourceBytes = css.size();
    appendCssRulesFromLowerCss(toLowerCopy(css), cache.rules);
    sourceBytes = cache.sourceBytes;
    const size_t count = std::min(cache.rules.size(), 512U - rulesOut.size());
    rulesOut.insert(rulesOut.end(), cache.rules.begin(), cache.rules.begin() + count);
    if (isEpubVirtualPath(path)) {
        size_t totalRules = cache.rules.size();
        for (const auto &entry : gExternalCssCaches)
            totalRules += entry.rules.size();
        while (!gExternalCssCaches.empty() && (gExternalCssCaches.size() >= 4U || totalRules > 512U)) {
            totalRules -= gExternalCssCaches.front().rules.size();
            gExternalCssCaches.erase(gExternalCssCaches.begin());
        }
        gExternalCssCaches.push_back(std::move(cache));
    }
    return true;
}

void parseCssRulesFromDocument(const std::string &text, const std::string &textLower, const std::string &baseDir,
                               std::vector<CssStyleRule> &rulesOut)
{
    rulesOut.clear();

    size_t inlineBudget = CSS_MAX_INLINE_TOTAL_BYTES;
    size_t searchPos = 0;
    while (inlineBudget > 0U && searchPos < textLower.size()) {
        const size_t styleOpen = textLower.find("<style", searchPos);
        if (styleOpen == std::string::npos) {
            break;
        }
        const size_t openGt = InkHUD::readerTagEnd(textLower, styleOpen + 6);
        if (openGt == std::string::npos) {
            break;
        }
        const size_t styleClose = textLower.find("</style", openGt + 1);
        if (styleClose == std::string::npos) {
            break;
        }

        const size_t chunkStart = openGt + 1;
        const size_t chunkLen = styleClose - chunkStart;
        const size_t toCopy = std::min(chunkLen, inlineBudget);
        if (toCopy > 0U) {
            appendCssRulesFromLowerCss(textLower.substr(chunkStart, toCopy), rulesOut);
            inlineBudget -= toCopy;
            if (rulesOut.size() >= 512U) {
                return;
            }
        }
        searchPos = styleClose + 8;
    }

    size_t externalBudget = CSS_MAX_EXTERNAL_TOTAL_BYTES;
    std::vector<std::string> loadedCssPaths;
    searchPos = 0;
    while (externalBudget > 0U && searchPos < textLower.size()) {
        const size_t linkOpen = textLower.find("<link", searchPos);
        if (linkOpen == std::string::npos) {
            break;
        }
        const size_t linkGt = InkHUD::readerTagEnd(textLower, linkOpen + 5);
        if (linkGt == std::string::npos) {
            break;
        }

        const std::string tagOriginal = text.substr(linkOpen + 1, linkGt - linkOpen - 1);
        const std::string rel = toLowerCopy(InkHUD::readerXmlAttribute(tagOriginal, "rel"));
        const std::string type = toLowerCopy(InkHUD::readerXmlAttribute(tagOriginal, "type"));
        const std::string href = InkHUD::readerXmlAttribute(tagOriginal, "href");

        const bool isStylesheet = (rel.find("stylesheet") != std::string::npos) || (type.find("text/css") != std::string::npos);
        if (isStylesheet && !href.empty()) {
            const std::string resolved = resolveRelativePath(baseDir, href);
            if (!resolved.empty() && !containsToken(loadedCssPaths, resolved)) {
                loadedCssPaths.push_back(resolved);
                size_t sourceBytes = 0;
                const size_t perFileLimit = std::min(CSS_MAX_EXTERNAL_FILE_BYTES, externalBudget);
                if (perFileLimit > 0U && appendExternalCssRules(resolved, perFileLimit, rulesOut, sourceBytes)) {
                    if (sourceBytes >= externalBudget) {
                        externalBudget = 0U;
                    } else {
                        externalBudget -= sourceBytes;
                    }
                    if (rulesOut.size() >= 512U) {
                        return;
                    }
                }
            }
        }

        searchPos = linkGt + 1;
    }
}

void accumulateCssStyleForTag(const std::vector<CssStyleRule> &rules, const std::string &tagNameLower, const std::string &idLower,
                              const std::vector<std::string> &classesLower, bool &bold, bool &underline, bool &strike,
                              CssIndent &indent, uint8_t &alignment, uint8_t &paragraphSpacing, bool &noUnderline,
                              int16_t &firstLineIndent)
{
    bold = false;
    underline = false;
    strike = false;
    indent = {};
    alignment = 0;
    paragraphSpacing = 0;
    noUnderline = false;
    firstLineIndent = -1;
    std::array<size_t, 3> marginSpecificity{}, paddingSpecificity{};

    for (const CssStyleRule &rule : rules) {
        if (!rule.tag.empty() && rule.tag != tagNameLower) {
            continue;
        }
        if (!rule.id.empty() && rule.id != idLower) {
            continue;
        }

        bool classesMatch = true;
        for (const std::string &cls : rule.classes) {
            if (!containsToken(classesLower, cls)) {
                classesMatch = false;
                break;
            }
        }
        if (!classesMatch) {
            continue;
        }

        bold = bold || rule.bold;
        underline = underline || rule.underline;
        strike = strike || rule.strike;
        const std::array<size_t, 3> specificity{!rule.id.empty(), rule.classes.size(), !rule.tag.empty()};
        if (rule.indent.margin >= 0 && specificity >= marginSpecificity) {
            indent.margin = rule.indent.margin;
            marginSpecificity = specificity;
        }
        if (rule.indent.padding >= 0 && specificity >= paddingSpecificity) {
            indent.padding = rule.indent.padding;
            paddingSpecificity = specificity;
        }
        if (rule.alignment)
            alignment = rule.alignment;
        if (rule.paragraphSpacing)
            paragraphSpacing = rule.paragraphSpacing;
        if (rule.noUnderline || rule.underline)
            noUnderline = rule.noUnderline;
        if (rule.firstLineIndent >= 0)
            firstLineIndent = rule.firstLineIndent;
    }
}

void appendArtifactPlaceholder(std::string &out, const std::string &kind, const std::string &label)
{
    if (!out.empty() && out.back() != '\n') {
        out.push_back('\n');
    }
    out.push_back('[');
    out += kind;
    if (!label.empty()) {
        out += ": ";
        out += label;
    }
    out.push_back(']');
    out.push_back('\n');
}

bool appendImageMarker(std::string &out, const std::string &baseDir, const std::string &reference)
{
    if (reference.empty()) {
        return false;
    }
    const std::string resolved = resolveRelativePath(baseDir, reference);
    if (!hasRenderableImageExtension(resolved)) {
        return false;
    }
    const std::string marker = makeImageMarkerLine(resolved);
    if (marker.empty()) {
        return false;
    }
    if (!out.empty() && out.back() != '\n') {
        out.push_back('\n');
    }
    out += marker;
    out.push_back('\n');
    return true;
}

bool appendArtifactPlaceholderForTag(std::string &out, const std::string &name, const std::string &tagOriginal,
                                     const std::string &baseDir)
{
    std::string kind;
    if (name == "img" || name == "image") {
        std::string imageRef = InkHUD::readerXmlAttribute(tagOriginal, "src");
        if (imageRef.empty()) {
            imageRef = InkHUD::readerXmlAttribute(tagOriginal, "href");
        }
        if (imageRef.empty()) {
            imageRef = InkHUD::readerXmlAttribute(tagOriginal, "xlink:href");
        }
        if (!imageRef.empty()) {
            if (appendImageMarker(out, baseDir, imageRef)) {
                return true;
            }
        }
        kind = "Image";
    } else if (name == "svg") {
        return false;
    } else if (name == "math") {
        kind = "Math";
    } else if (name == "canvas") {
        kind = "Canvas";
    } else if (name == "iframe" || name == "object" || name == "embed") {
        kind = "Embedded";
    } else {
        return false;
    }

    std::string label = compactTextForDisplay(InkHUD::readerXmlAttribute(tagOriginal, "alt"));
    if (label.empty()) {
        label = compactTextForDisplay(InkHUD::readerXmlAttribute(tagOriginal, "title"));
    }
    if (label.empty()) {
        label = compactTextForDisplay(InkHUD::readerXmlAttribute(tagOriginal, "aria-label"));
    }
    if (label.empty()) {
        label = leafNameFromReference(InkHUD::readerXmlAttribute(tagOriginal, "src"));
    }
    if (label.empty()) {
        label = leafNameFromReference(InkHUD::readerXmlAttribute(tagOriginal, "href"));
    }

    appendArtifactPlaceholder(out, kind, label);
    return true;
}

std::string compactTextForDisplay(const std::string &input, std::vector<std::pair<std::string, size_t>> *anchors)
{
    std::string out;
    out.reserve(input.size());

    std::vector<std::pair<size_t, size_t>> anchorPositions;
    if (anchors) {
        for (size_t i = 0; i < anchors->size(); ++i)
            if ((*anchors)[i].second != std::string::npos)
                anchorPositions.emplace_back((*anchors)[i].second, i);
        std::sort(anchorPositions.begin(), anchorPositions.end());
    }
    size_t nextAnchor = 0;
    auto mapAnchors = [&](size_t position) {
        while (nextAnchor < anchorPositions.size() && anchorPositions[nextAnchor].first <= position) {
            (*anchors)[anchorPositions[nextAnchor].second].second = out.size();
            ++nextAnchor;
        }
    };

    bool prevSpace = false;
    uint8_t newlineRun = 0;
    uint32_t yieldCheckpoint = 0;

    for (uint32_t index = 0; index < input.size(); index++) {
        mapAnchors(index);
        yieldDuringBookReaderWork(yieldCheckpoint, index);
        if (static_cast<uint8_t>(input[index]) == 0xE2 && input.compare(index, 3, "\xE2\x80\xA6") == 0) {
            out += "...";
            index += 2;
            prevSpace = false;
            newlineRun = 0;
            continue;
        }
        const size_t spaceLength = NicheGraphics::InkHUD::readerSpaceLengthAt(input, index);
        const char c = spaceLength ? ' ' : input[index];
        if (spaceLength)
            index += spaceLength - 1U;
        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            while (!out.empty() && out.back() == ' ') {
                out.pop_back();
            }
            if (newlineRun < 2) {
                out.push_back('\n');
                newlineRun++;
            }
            prevSpace = false;
            continue;
        }

        if (c == '\t' || c == ' ') {
            if (out.empty() || out.back() == '\n' || prevSpace) {
                continue;
            }
            out.push_back(' ');
            prevSpace = true;
            newlineRun = 0;
            continue;
        }

        out.push_back(c);
        prevSpace = false;
        newlineRun = 0;
    }

    mapAnchors(input.size());
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) {
        out.pop_back();
    }

    return out;
}

bool isBlockTag(const std::string &name)
{
    static const char *TAGS[] = {"br",      "p",       "div",        "li",    "ul",       "ol",         "tr",
                                 "table",   "h1",      "h2",         "h3",    "h4",       "h5",         "h6",
                                 "section", "article", "blockquote", "title", "subtitle", "empty-line", "pre"};
    for (const char *tag : TAGS) {
        if (name == tag) {
            return true;
        }
    }
    return false;
}

std::string decodeHtmlEntity(const std::string &entity)
{
    if (entity == "amp") {
        return "&";
    }
    if (entity == "lt") {
        return "<";
    }
    if (entity == "gt") {
        return ">";
    }
    if (entity == "quot") {
        return "\"";
    }
    if (entity == "apos") {
        return "'";
    }
    if (entity == "nbsp") {
        return " ";
    }
    if (entity == "hellip" || entity == "#8230" || entity == "#x2026" || entity == "#X2026")
        return "...";
    return "";
}

size_t findCaseInsensitive(const std::string &haystack, const std::string &needleLower, size_t startPos)
{
    if (needleLower.empty() || startPos >= haystack.size()) {
        return std::string::npos;
    }
    if (needleLower.size() > haystack.size()) {
        return std::string::npos;
    }

    const size_t maxStart = haystack.size() - needleLower.size();
    for (size_t i = startPos; i <= maxStart; i++) {
        bool match = true;
        for (size_t j = 0; j < needleLower.size(); j++) {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(haystack[i + j])));
            if (c != needleLower[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            return i;
        }
    }
    return std::string::npos;
}

BOOKREADER_NOINLINE std::string stripHtmlLike(const std::string &text, const std::string &baseDir,
                                              std::vector<std::pair<std::string, size_t>> *anchors = nullptr,
                                              bool *incompleteTable = nullptr)
{
    struct OpenTagStyle {
        std::string name;
        bool bold = false;
        bool underline = false;
        bool strike = false;
        uint16_t indentPx = 0;
        uint8_t alignment = 0;
        bool compactParagraph = false;
        bool tableGrid = false;
        int16_t firstLineIndent = -1;
        int32_t nextListNumber = 1;
    };

    std::string out;
    out.reserve(text.size());
    std::vector<CssStyleRule> cssRules;
    {
        // Bound CSS discovery without dropping styles from larger chapters.
        const std::string textLower = toLowerCopy(text.substr(0, CSS_MAX_SOURCE_TEXT_BYTES));
        parseCssRulesFromDocument(text, textLower, baseDir, cssRules);
    }
    std::vector<OpenTagStyle> openTags;
    std::string hiddenTag;
    uint32_t hiddenDepth = 0;

    uint16_t activeIndentPx = 0;
    uint8_t activeAlignment = 1;
    int32_t boldDepth = 0;
    int32_t underlineDepth = 0;
    int32_t strikeDepth = 0;
    bool atLineStart = true;
    bool paragraphFirstLine = true;
    bool listLabelPending = false;
    uint32_t yieldCheckpoint = 0;

    auto restoreAlignment = [&]() {
        activeAlignment = 1;
        for (auto tag = openTags.rbegin(); tag != openTags.rend(); ++tag) {
            if (tag->alignment) {
                activeAlignment = tag->alignment;
                break;
            }
        }
    };

    auto appendLineStartIndent = [&]() {
        uint16_t firstIndent = 0;
        if (atLineStart && paragraphFirstLine) {
            for (auto tag = openTags.rbegin(); tag != openTags.rend(); ++tag) {
                if (tag->firstLineIndent >= 0) {
                    firstIndent = static_cast<uint16_t>(tag->firstLineIndent);
                    break;
                }
            }
        }
        if (atLineStart && (activeIndentPx > 0 || activeAlignment > 1 || firstIndent > 0)) {
            // Style tokens may already precede the first visible character.
            const size_t newline = out.find_last_of('\n');
            const size_t start = newline == std::string::npos ? 0 : newline + 1;
            out.insert(start,
                       makeLineXOffsetMarker(activeIndentPx) +
                           (activeAlignment == 2   ? ALIGN_CENTER
                            : activeAlignment == 3 ? ALIGN_RIGHT
                                                   : "") +
                           (firstIndent ? std::string(InkHUD::FIRST_LINE_MARKER) + makeLineXOffsetMarker(firstIndent) : ""));
            atLineStart = false;
        }
        paragraphFirstLine = false;
    };

    auto appendNewline = [&](bool compact = false) {
        const bool inCell = std::any_of(openTags.begin(), openTags.end(),
                                        [](const OpenTagStyle &tag) { return tag.name == "td" || tag.name == "th"; });
        const bool inList = std::any_of(openTags.begin(), openTags.end(),
                                        [](const OpenTagStyle &tag) { return tag.name == "ul" || tag.name == "ol"; });
        const bool preformatted =
            std::any_of(openTags.begin(), openTags.end(), [](const OpenTagStyle &tag) { return tag.name == "pre"; });
        if (inCell && !inList && !preformatted) {
            if (!out.empty() && out.back() != ' ' && out.back() != '\n')
                out.push_back(' ');
            return;
        }
        InkHUD::appendHtmlBlockBreak(out, inList || compact);
        atLineStart = true;
    };

    auto appendStyleMarker = [&](InlineStyleMarker marker) {
        if (marker == InlineStyleMarker::BOLD_ON) {
            out += STYLE_BOLD_ON;
        } else if (marker == InlineStyleMarker::BOLD_OFF) {
            out += STYLE_BOLD_OFF;
        } else if (marker == InlineStyleMarker::UNDERLINE_ON) {
            out += STYLE_UNDERLINE_ON;
        } else if (marker == InlineStyleMarker::UNDERLINE_OFF) {
            out += STYLE_UNDERLINE_OFF;
        } else if (marker == InlineStyleMarker::STRIKE_ON) {
            out += STYLE_STRIKE_ON;
        } else if (marker == InlineStyleMarker::STRIKE_OFF) {
            out += STYLE_STRIKE_OFF;
        }
    };

    auto changeStyleDepth = [&](int32_t &depth, bool begin, InlineStyleMarker on, InlineStyleMarker off) {
        if (begin) {
            if (depth++ == 0)
                appendStyleMarker(on);
        } else if (depth > 0 && --depth == 0) {
            appendStyleMarker(off);
        }
    };
    auto beginBold = [&]() { changeStyleDepth(boldDepth, true, InlineStyleMarker::BOLD_ON, InlineStyleMarker::BOLD_OFF); };
    auto endBold = [&]() { changeStyleDepth(boldDepth, false, InlineStyleMarker::BOLD_ON, InlineStyleMarker::BOLD_OFF); };
    auto beginUnderline = [&]() {
        changeStyleDepth(underlineDepth, true, InlineStyleMarker::UNDERLINE_ON, InlineStyleMarker::UNDERLINE_OFF);
    };
    auto endUnderline = [&]() {
        changeStyleDepth(underlineDepth, false, InlineStyleMarker::UNDERLINE_ON, InlineStyleMarker::UNDERLINE_OFF);
    };
    auto beginStrike = [&]() {
        changeStyleDepth(strikeDepth, true, InlineStyleMarker::STRIKE_ON, InlineStyleMarker::STRIKE_OFF);
    };
    auto endStrike = [&]() { changeStyleDepth(strikeDepth, false, InlineStyleMarker::STRIKE_ON, InlineStyleMarker::STRIKE_OFF); };

    auto closeTag = [&](const std::string &name) {
        for (size_t idx = openTags.size(); idx > 0; --idx) {
            if (openTags[idx - 1].name != name)
                continue;
            const OpenTagStyle closed = openTags[idx - 1];
            openTags.erase(openTags.begin() + static_cast<std::ptrdiff_t>(idx - 1));
            if (closed.bold)
                endBold();
            if (closed.underline)
                endUnderline();
            if (closed.strike)
                endStrike();
            activeIndentPx -= std::min(activeIndentPx, closed.indentPx);
            if (closed.alignment)
                restoreAlignment();
            break;
        }
    };

    size_t i = 0;
    while (i < text.size()) {
        yieldDuringBookReaderWork(yieldCheckpoint, static_cast<uint32_t>(i));
        const char c = text[i];

        if (hiddenDepth && c != '<') {
            i = text.find('<', i);
            if (i == std::string::npos)
                break;
            continue;
        }

        if (c == '<') {
            const size_t end = InkHUD::readerTagEnd(text, i + 1);
            if (end == std::string::npos) {
                break;
            }

            const std::string tagOriginal = text.substr(i + 1, end - i - 1);
            std::string tag = toLowerCopy(tagOriginal);
            if (tag.rfind("!--", 0) == 0) {
                i = end + 1;
                continue;
            }

            size_t pos = 0;
            while (pos < tag.size() && (tag[pos] == ' ' || tag[pos] == '\t')) {
                pos++;
            }
            bool isClosingTag = false;
            if (pos < tag.size() && tag[pos] == '/') {
                isClosingTag = true;
                pos++;
            }
            while (pos < tag.size() && (tag[pos] == ' ' || tag[pos] == '\t')) {
                pos++;
            }
            size_t nameEnd = pos;
            while (nameEnd < tag.size() && isTagNameChar(tag[nameEnd])) {
                nameEnd++;
            }
            const std::string name = tag.substr(pos, nameEnd - pos);
            const bool isSelfClosing = (!isClosingTag && !tag.empty() && tag.back() == '/');
            if (hiddenDepth) {
                if (name == hiddenTag) {
                    if (isClosingTag)
                        --hiddenDepth;
                    else if (!isSelfClosing)
                        ++hiddenDepth;
                }
                i = end + 1;
                continue;
            }
            bool compactParagraph = name == "p" && !isClosingTag && InkHUD::readerParagraphStartsWithBullet(text, end + 1);
            if (name == "p" && isClosingTag) {
                const auto paragraph =
                    std::find_if(openTags.rbegin(), openTags.rend(), [](const OpenTagStyle &tag) { return tag.name == "p"; });
                compactParagraph = paragraph != openTags.rend() && paragraph->compactParagraph;
            }
            const std::string inlineStyle = toLowerCopy(InkHUD::readerXmlAttribute(tagOriginal, "style"));
            if (!isClosingTag && cssPropertyValue(inlineStyle, "display") == "none") {
                if (!isSelfClosing && name != "img" && name != "br" && name != "hr" && name != "input" && name != "meta" &&
                    name != "link" && name != "source" && name != "area" && name != "base" && name != "col" && name != "embed" &&
                    name != "param" && name != "track" && name != "wbr") {
                    hiddenTag = name;
                    hiddenDepth = 1;
                }
                i = end + 1;
                continue;
            }
            const std::string idAttr = toLowerCopy(InkHUD::readerXmlAttribute(tagOriginal, "id"));
            bool tableGrid = false;
            for (auto opened = openTags.rbegin(); opened != openTags.rend(); ++opened) {
                if (opened->name == "table") {
                    tableGrid = opened->tableGrid;
                    break;
                }
            }
            if (!isClosingTag && name == "table") {
                const size_t close = findCaseInsensitive(text, "</table", end + 1);
                const bool completeClose = close != std::string::npos && InkHUD::readerTagEnd(text, close) != std::string::npos;
                if (incompleteTable && !completeClose) {
                    *incompleteTable = true;
                    return {};
                }
                tableGrid = completeClose;
                for (const char *unsupported : {"<table", "<img", "<image", "<svg", "<ul", "<ol", "<pre"})
                    if (findCaseInsensitive(text, unsupported, end + 1) < close)
                        tableGrid = false;
                for (size_t cell = end + 1; tableGrid && (cell = text.find('<', cell)) < close;) {
                    const size_t cellEnd = InkHUD::readerTagEnd(text, cell);
                    if (cellEnd == std::string::npos || cellEnd >= close)
                        break;
                    const std::string cellTag = text.substr(cell + 1, cellEnd - cell - 1);
                    const std::string cellLower = toLowerCopy(cellTag);
                    if (cellLower.size() > 2 && cellLower[0] == 't' && (cellLower[1] == 'd' || cellLower[1] == 'h') &&
                        std::isspace(static_cast<unsigned char>(cellLower[2]))) {
                        for (const char *attribute : {"colspan", "rowspan"}) {
                            const std::string span = InkHUD::readerXmlAttribute(cellTag, attribute);
                            if (!span.empty() && span != "1")
                                tableGrid = false;
                        }
                    }
                    cell = cellEnd + 1;
                }
                if (std::any_of(openTags.begin(), openTags.end(), [](const auto &opened) { return opened.name == "table"; }))
                    tableGrid = false;
            }
            if (tableGrid && name == "tr")
                compactParagraph = true;
            const std::string classAttr = toLowerCopy(InkHUD::readerXmlAttribute(tagOriginal, "class"));
            std::vector<std::string> classTokens;
            splitClassTokens(classAttr, classTokens);
            const bool impliedBoldTag = (name == "b" || name == "strong");
            const bool impliedUnderlineTag = (name == "u" || name == "ins" || name == "a");
            const bool impliedStrikeTag = (name == "s" || name == "strike" || name == "del");
            const bool impliedHeadingTag =
                (name == "h1" || name == "h2" || name == "h3" || name == "h4" || name == "h5" || name == "h6" || name == "th");
            const bool styleBold = cssWantsBold(inlineStyle);
            const bool styleUnderline = cssWantsUnderline(inlineStyle);
            const bool styleStrike = cssWantsStrike(inlineStyle);
            bool cssBold = false;
            bool cssUnderline = false;
            bool cssStrike = false;
            CssIndent indent;
            uint8_t alignment = 0;
            uint8_t paragraphSpacing = 0;
            bool noUnderline = false;
            int16_t firstLineIndent = -1;
            accumulateCssStyleForTag(cssRules, name, idAttr, classTokens, cssBold, cssUnderline, cssStrike, indent, alignment,
                                     paragraphSpacing, noUnderline, firstLineIndent);
            if (const int16_t inlineFirstIndent = cssFirstLineIndent(inlineStyle); inlineFirstIndent >= 0)
                firstLineIndent = inlineFirstIndent;
            if (const uint8_t inlineSpacing = cssParagraphSpacing(inlineStyle))
                paragraphSpacing = inlineSpacing;
            if (!isClosingTag && name == "p" && paragraphSpacing == 1)
                compactParagraph = true;
            if (cssPropertyValue(inlineStyle, "text-decoration") == "none")
                noUnderline = true;
            else if (styleUnderline)
                noUnderline = false;
            if (const uint8_t inlineAlignment = cssAlignment(inlineStyle))
                alignment = inlineAlignment;
            indent.apply(cssIndentPixels(inlineStyle));
            uint16_t styleIndentPx = indent.pixels();
            if (name == "blockquote") {
                styleIndentPx = std::max<uint16_t>(styleIndentPx, STYLE_INDENT_STEP_PX);
            }
            if (name == "ul" || name == "ol") {
                styleIndentPx = std::max<uint16_t>(styleIndentPx, STYLE_INDENT_STEP_PX);
            }

            if (!isClosingTag && (name == "style" || name == "script" || name == "head" || name == "title")) {
                const std::string closeNeedle = "</" + name;
                const size_t closeTag = findCaseInsensitive(text, closeNeedle, end + 1);
                if (closeTag == std::string::npos) {
                    break;
                }
                const size_t closeGt = InkHUD::readerTagEnd(text, closeTag + closeNeedle.size());
                if (closeGt == std::string::npos) {
                    break;
                }
                i = closeGt + 1;
                continue;
            }

            if (!name.empty() && isBlockTag(name)) {
                if (!isClosingTag && name != "br")
                    paragraphFirstLine = true;
                if (isClosingTag && name == "li")
                    listLabelPending = false;
                if (isClosingTag && (name == "blockquote" || name == "ul" || name == "ol")) {
                    closeTag(name);
                }
                // A paragraph inside an item must not separate its label from its text.
                const bool closeAfterStyles = isClosingTag && name != "blockquote" && name != "ul" && name != "ol";
                if (!closeAfterStyles && !(listLabelPending && !isClosingTag && (name == "p" || name == "div"))) {
                    appendNewline(compactParagraph);
                }
            }

            if (!isClosingTag && !name.empty()) {
                if (anchors) {
                    std::string id = InkHUD::readerXmlAttribute(tagOriginal, "id");
                    if (id.empty() && name == "a")
                        id = InkHUD::readerXmlAttribute(tagOriginal, "name");
                    for (auto &anchor : *anchors) {
                        if (anchor.first == id && anchor.second == std::string::npos)
                            anchor.second = out.size();
                    }
                }
                if (appendArtifactPlaceholderForTag(out, name, tagOriginal, baseDir))
                    atLineStart = true;
                if (tableGrid) {
                    if (name == "table") {
                        out += InkHUD::TABLE_START;
                        appendNewline(true);
                    } else if (name == "tr") {
                        out += InkHUD::TABLE_ROW;
                        atLineStart = false;
                    } else if (name == "td" || name == "th") {
                        out += InkHUD::TABLE_CELL;
                        atLineStart = false;
                    }
                }
            }

            if (isClosingTag && !name.empty() && !(name == "blockquote" || name == "ul" || name == "ol")) {
                closeTag(name);
            } else if (!isClosingTag && !isSelfClosing && !name.empty()) {
                OpenTagStyle opened;
                opened.name = name;
                opened.tableGrid = name == "table" && tableGrid;
                opened.bold = impliedBoldTag || impliedHeadingTag || styleBold || cssBold;
                opened.underline = (impliedUnderlineTag || styleUnderline || cssUnderline) && !noUnderline;
                opened.strike = impliedStrikeTag || styleStrike || cssStrike;
                opened.indentPx = styleIndentPx;
                opened.alignment = alignment;
                opened.compactParagraph = compactParagraph;
                if (isBlockTag(name) || name == "body")
                    opened.firstLineIndent = firstLineIndent;
                if (alignment)
                    activeAlignment = alignment;

                if (opened.bold) {
                    beginBold();
                }
                if (opened.underline) {
                    beginUnderline();
                }
                if (opened.strike) {
                    beginStrike();
                }
                if (opened.indentPx > 0) {
                    uint32_t nextIndent = static_cast<uint32_t>(activeIndentPx) + opened.indentPx;
                    activeIndentPx = static_cast<uint16_t>(std::min<uint32_t>(STYLE_MAX_INDENT_PX, nextIndent));
                }

                openTags.push_back(std::move(opened));
                if (name == "li") {
                    appendLineStartIndent();
                    auto list = std::find_if(openTags.rbegin(), openTags.rend(),
                                             [](const OpenTagStyle &tag) { return tag.name == "ul" || tag.name == "ol"; });
                    if (list != openTags.rend() && list->name == "ol") {
                        out += std::to_string(list->nextListNumber++) + ". ";
                    } else {
                        out += NicheGraphics::InkHUD::BULLET_MARKER;
                    }
                    listLabelPending = true;
                    atLineStart = false;
                }
            }

            if (isClosingTag && isBlockTag(name) && name != "blockquote" && name != "ul" && name != "ol") {
                if (name == "table" && tableGrid)
                    out += InkHUD::TABLE_END;
                appendNewline(compactParagraph);
            }
            i = end + 1;
            continue;
        }

        if (atLineStart) {
            const size_t bulletLength = InkHUD::readerBulletLengthAt(text, i);
            if (bulletLength) {
                appendLineStartIndent();
                out += InkHUD::BULLET_MARKER;
                atLineStart = false;
                i += bulletLength;
                continue;
            }
        }

        if (c == '&') {
            const size_t end = text.find(';', i + 1);
            if (end != std::string::npos && (end - i) <= 10) {
                const std::string entity = text.substr(i + 1, end - i - 1);
                const std::string decoded = decodeHtmlEntity(entity);
                if (!decoded.empty()) {
                    if (decoded == "\n") {
                        appendNewline();
                    } else {
                        appendLineStartIndent();
                        out += decoded;
                        listLabelPending = false;
                        atLineStart = false;
                    }
                    i = end + 1;
                    continue;
                }
            }
        }

        if (std::isspace(static_cast<unsigned char>(c))) {
            const bool preformatted =
                std::any_of(openTags.begin(), openTags.end(), [](const OpenTagStyle &tag) { return tag.name == "pre"; });
            if (preformatted && (c == '\n' || c == '\r')) {
                if (c != '\r' || i + 1 >= text.size() || text[i + 1] != '\n')
                    appendNewline();
                i++;
                continue;
            }
            if (!atLineStart && !out.empty() && out.back() != ' ') {
                out.push_back(' ');
            }
            i++;
            continue;
        }

        appendLineStartIndent();
        out.push_back(c);
        listLabelPending = false;
        atLineStart = false;
        i++;
    }

    while (!openTags.empty()) {
        const OpenTagStyle opened = openTags.back();
        openTags.pop_back();
        if (opened.bold) {
            endBold();
        }
        if (opened.underline) {
            endUnderline();
        }
        if (opened.strike) {
            endStrike();
        }
    }

    return compactTextForDisplay(out, anchors);
}

int hexNibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

std::string stripRtf(const std::string &text)
{
    std::string out;
    out.reserve(text.size());

    size_t i = 0;
    uint32_t yieldCheckpoint = 0;
    while (i < text.size()) {
        yieldDuringBookReaderWork(yieldCheckpoint, static_cast<uint32_t>(i));
        const char c = text[i];

        if (c == '{' || c == '}') {
            i++;
            continue;
        }

        if (c == '\\') {
            if (i + 1 >= text.size()) {
                i++;
                continue;
            }

            const char next = text[i + 1];
            if (next == '\\' || next == '{' || next == '}') {
                out.push_back(next);
                i += 2;
                continue;
            }

            if (next == '\'' && i + 3 < text.size()) {
                const int hi = hexNibble(text[i + 2]);
                const int lo = hexNibble(text[i + 3]);
                if (hi >= 0 && lo >= 0) {
                    out.push_back(static_cast<char>((hi << 4) | lo));
                    i += 4;
                    continue;
                }
            }

            i++;
            while (i < text.size() && std::isalpha((unsigned char)text[i])) {
                i++;
            }
            if (i < text.size() && (text[i] == '-' || std::isdigit((unsigned char)text[i]))) {
                i++;
                while (i < text.size() && std::isdigit((unsigned char)text[i])) {
                    i++;
                }
            }
            if (i < text.size() && text[i] == ' ') {
                i++;
            }
            continue;
        }

        out.push_back(c);
        i++;
    }

    return compactTextForDisplay(out);
}

bool isMeaningfulDisplayLine(const std::string &line)
{
    size_t start = 0;
    consumeLeadingLineXOffsetMarkers(line, start);
    while (start < line.size()) {
        size_t markerLength = 0;
        if (parseInlineStyleMarkerAt(line, start, markerLength) != InlineStyleMarker::NONE) {
            start += markerLength;
            continue;
        }
        if (!std::isspace(static_cast<unsigned char>(line[start++]))) {
            return true;
        }
    }
    return false;
}

std::string collapseImageOnlyPageToSingleMarker(const std::string &parsed)
{
    if (parsed.empty()) {
        return parsed;
    }

    std::vector<std::string> imagePaths;
    size_t lineStart = 0;
    while (lineStart <= parsed.size()) {
        const size_t lineEnd = parsed.find('\n', lineStart);
        const bool hasNewline = lineEnd != std::string::npos;
        const size_t end = hasNewline ? lineEnd : parsed.size();
        const std::string rawLine = parsed.substr(lineStart, end - lineStart);
        size_t visibleStart = 0;
        consumeLeadingLineXOffsetMarkers(rawLine, visibleStart);
        const std::string line = stripInlineStyleMarkers(rawLine.substr(visibleStart));

        std::string imagePath;
        if (isImageMarkerLine(line, &imagePath)) {
            if (!imagePath.empty()) {
                imagePaths.push_back(imagePath);
            }
        } else if (!line.empty() && !isImageContinuationLine(line) && isMeaningfulDisplayLine(line)) {
            return parsed;
        }

        if (!hasNewline) {
            break;
        }
        lineStart = end + 1;
    }

    if (imagePaths.empty()) {
        return parsed;
    }
    if (imagePaths.size() == 1U) {
        return makeImageMarkerLine(imagePaths.front());
    }

    return makeImageSequenceMarkerLine(imagePaths);
}

BOOKREADER_NOINLINE std::string extractDisplayText(const std::string &raw, InkHUD::BookReaderApplet::BookFormat format,
                                                   const std::string &sourcePath, bool *incompleteTable = nullptr)
{
    const std::string sourceDir = parentDir(sourcePath);
    switch (format) {
    case InkHUD::BookReaderApplet::BookFormat::HTML_LIKE:
        return collapseImageOnlyPageToSingleMarker(stripHtmlLike(raw, sourceDir, nullptr, incompleteTable));
    case InkHUD::BookReaderApplet::BookFormat::RTF:
        return stripRtf(raw);
    case InkHUD::BookReaderApplet::BookFormat::PLAIN_TEXT:
    default:
        return compactTextForDisplay(raw);
    }
}

void parseContentsNavigation(const std::string &xml, const std::string &baseDir, bool ncx,
                             std::vector<InkHUD::BookReaderApplet::ContentsEntry> &entries)
{
    std::vector<size_t> stack;
    int listDepth = 0;
    size_t begin = 0, limit = xml.size();
    if (!ncx) {
        size_t nav = 0;
        while ((nav = findCaseInsensitive(xml, "<nav", nav)) != std::string::npos) {
            const size_t end = InkHUD::readerTagEnd(xml, nav);
            if (end == std::string::npos)
                break;
            const std::string tag = xml.substr(nav, end - nav + 1);
            if (InkHUD::readerXmlAttribute(tag, "epub:type") == "toc" || InkHUD::readerXmlAttribute(tag, "role") == "doc-toc") {
                begin = end + 1;
                const size_t close = findCaseInsensitive(xml, "</nav", begin);
                if (close != std::string::npos)
                    limit = close;
                break;
            }
            nav = end + 1;
        }
    }
    auto setTarget = [&](InkHUD::BookReaderApplet::ContentsEntry &entry, const std::string &href) {
        const size_t hash = href.find('#');
        entry.path = resolveRelativePath(baseDir, href.substr(0, hash));
        if (hash != std::string::npos)
            entry.fragment = decodeUrlComponent(href.substr(hash + 1));
    };
    for (size_t pos = begin; pos < limit && entries.size() < 2048;) {
        const size_t start = xml.find('<', pos);
        if (start == std::string::npos || start >= limit)
            break;
        const size_t end = InkHUD::readerTagEnd(xml, start);
        if (end == std::string::npos)
            break;
        const std::string tag = xml.substr(start, end - start + 1);
        const std::string lower = toLowerCopy(tag);
        size_t nameStart = 1;
        const bool closing = lower.size() > 1 && lower[1] == '/';
        if (closing)
            ++nameStart;
        const size_t nameEnd = lower.find_first_of(" />\t\r\n", nameStart);
        const std::string name = lower.substr(nameStart, nameEnd - nameStart);
        pos = end + 1;
        if (ncx) {
            if (name == "navpoint") {
                if (closing) {
                    if (!stack.empty())
                        stack.pop_back();
                } else {
                    InkHUD::BookReaderApplet::ContentsEntry entry;
                    entry.depth = std::min<size_t>(stack.size(), 32);
                    entries.push_back(std::move(entry));
                    stack.push_back(entries.size() - 1);
                }
            } else if (!closing && !stack.empty()) {
                auto &entry = entries[stack.back()];
                if (name == "content")
                    setTarget(entry, InkHUD::readerXmlAttribute(tag, "src"));
                else if (name == "text") {
                    const size_t close = findCaseInsensitive(xml, "</text", pos);
                    if (close != std::string::npos)
                        entry.title = stripTagsSimple(xml.substr(pos, close - pos));
                }
            }
        } else if (name == "ol" || name == "ul") {
            listDepth = std::max(0, listDepth + (closing ? -1 : 1));
        } else if (!closing && name == "a") {
            const size_t close = findCaseInsensitive(xml, "</a>", pos);
            if (close == std::string::npos || close > limit)
                continue;
            InkHUD::BookReaderApplet::ContentsEntry entry;
            entry.title = stripTagsSimple(xml.substr(pos, close - pos));
            entry.depth = std::min(32, std::max(0, listDepth - 1));
            setTarget(entry, InkHUD::readerXmlAttribute(tag, "href"));
            entries.push_back(std::move(entry));
            pos = close + 4;
        }
    }
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [](const auto &entry) { return entry.title.empty() || entry.path.empty(); }),
                  entries.end());
}

bool opfUsesPageNavigation(const std::string &opfText)
{
    size_t pos = 0;
    while ((pos = findCaseInsensitive(opfText, "<meta", pos)) != std::string::npos) {
        const size_t end = InkHUD::readerTagEnd(opfText, pos);
        if (end == std::string::npos)
            break;
        const std::string tag = opfText.substr(pos, end - pos + 1);
        const std::string name = toLowerCopy(InkHUD::readerXmlAttribute(tag, "name"));
        const std::string content = toLowerCopy(trimCopy(InkHUD::readerXmlAttribute(tag, "content")));
        if ((name == "book-type" && content == "comic") || (name == "fixed-layout" && content == "true"))
            return true;
        if (toLowerCopy(InkHUD::readerXmlAttribute(tag, "property")) == "rendition:layout") {
            const size_t textEnd = opfText.find('<', end + 1);
            if (textEnd != std::string::npos &&
                toLowerCopy(trimCopy(opfText.substr(end + 1, textEnd - end - 1))) == "pre-paginated")
                return true;
        }
        pos = end + 1;
    }
    return false;
}

std::string parseOpfCoverImagePath(const std::string &opfText, const std::string &opfDir)
{
    std::string coverId;
    size_t pos = 0;
    while ((pos = findCaseInsensitive(opfText, "<meta", pos)) != std::string::npos) {
        const size_t end = InkHUD::readerTagEnd(opfText, pos);
        if (end == std::string::npos)
            break;
        const std::string tag = opfText.substr(pos, end - pos + 1);
        if (toLowerCopy(InkHUD::readerXmlAttribute(tag, "name")) == "cover")
            coverId = InkHUD::readerXmlAttribute(tag, "content");
        pos = end + 1;
    }

    pos = 0;
    while ((pos = findCaseInsensitive(opfText, "<item", pos)) != std::string::npos) {
        const size_t end = InkHUD::readerTagEnd(opfText, pos);
        if (end == std::string::npos)
            break;
        const std::string tag = opfText.substr(pos, end - pos + 1);
        std::vector<std::string> properties;
        splitClassTokens(InkHUD::readerXmlAttribute(tag, "properties"), properties);
        const std::string id = InkHUD::readerXmlAttribute(tag, "id");
        if (containsToken(properties, "cover-image") || (!coverId.empty() && id == coverId)) {
            const std::string path = resolveRelativePath(opfDir, InkHUD::readerXmlAttribute(tag, "href"));
            if (hasRenderableImageExtension(path))
                return path;
        }
        pos = end + 1;
    }
    return "";
}

void parseOpfSpineOrder(const std::string &opfText, const std::string &opfDir, std::vector<std::string> &orderedPaths)
{
    struct ItemRef {
        std::string id;
        std::string path;
    };

    std::vector<ItemRef> manifest;
    manifest.reserve(256);
    orderedPaths.clear();

    size_t pos = 0;
    while (true) {
        const size_t itemPos = opfText.find("<item", pos);
        if (itemPos == std::string::npos) {
            break;
        }
        const size_t tagEnd = InkHUD::readerTagEnd(opfText, itemPos);
        if (tagEnd == std::string::npos) {
            break;
        }

        const std::string tag = opfText.substr(itemPos, tagEnd - itemPos + 1);
        const std::string id = InkHUD::readerXmlAttribute(tag, "id");
        const std::string href = InkHUD::readerXmlAttribute(tag, "href");
        if (!id.empty() && !href.empty()) {
            const std::string resolved = resolveRelativePath(opfDir, href);
            if (hasSupportedContinuousChapterExtension(resolved)) {
                manifest.push_back({id, resolved});
            }
        }
        pos = tagEnd + 1;
    }

    pos = 0;
    while (true) {
        const size_t refPos = opfText.find("<itemref", pos);
        if (refPos == std::string::npos) {
            break;
        }
        const size_t tagEnd = InkHUD::readerTagEnd(opfText, refPos);
        if (tagEnd == std::string::npos) {
            break;
        }

        const std::string tag = opfText.substr(refPos, tagEnd - refPos + 1);
        const std::string idref = InkHUD::readerXmlAttribute(tag, "idref");
        if (!idref.empty()) {
            for (const ItemRef &item : manifest) {
                if (item.id == idref) {
                    orderedPaths.push_back(item.path);
                    break;
                }
            }
        }
        pos = tagEnd + 1;
    }
}

std::string pickPreferredTocPath(const std::vector<std::string> &tocPaths)
{
    if (tocPaths.empty()) {
        return "";
    }
    for (const std::string &path : tocPaths) {
        const std::string lower = toLowerCopy(path);
        if (lower.size() >= 4 && lower.rfind(".ncx") == lower.size() - 4) {
            return path;
        }
    }
    return tocPaths.front();
}

} // namespace

void InkHUD::BookReaderApplet::setDisplayRefreshActive(bool active)
{
#if INKHUD_BOOKREADER_HAS_SD && INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES && INKHUD_BOOKREADER_USE_JPEGDEC
    const bool previous = gReaderDisplayRefreshActive.exchange(active);
    if (previous != active && gJpegPrefetchJob && !gJpegPrefetchJob->done.load())
        BR_PERF_LOG("JPEG prefetch display priority active=%u", active);
#else
    (void)active;
#endif
}

InkHUD::BookReaderApplet::BookReaderApplet() : concurrency::OSThread("ReaderPagination")
{
    concurrency::OSThread::disable();
    name = "Book Reader";
    currentDir = BOOKS_DIR;
}

void InkHUD::BookReaderApplet::resetPageIndex()
{
    concurrency::OSThread::disable();
    partPageCounts.clear();
    pageIndexWidth = pageIndexHeight = 0;
    pageIndexNext = 0;
    pageIndexText.clear();
    pageIndexImageOffset = 0;
    pageIndexSaved = false;
}

uint64_t InkHUD::BookReaderApplet::pageIndexFingerprint() const
{
    uint64_t hash = 14695981039346656037ULL;
    auto bytes = [&](const void *data, size_t size) {
        const auto *p = static_cast<const uint8_t *>(data);
        for (size_t i = 0; i < size; ++i)
            hash = (hash ^ p[i]) * 1099511628211ULL;
    };
    bytes(bookPath.data(), bookPath.size());
    const uint8_t readingHeight = fontSmall.lineHeight();
    const uint8_t footerHeight = fontSmall.lineHeight();
    bytes(&readingHeight, sizeof(readingHeight));
    bytes(&footerHeight, sizeof(footerHeight));
    if (fontSmall.gfxFont)
        bytes(fontSmall.gfxFont->glyph, (fontSmall.gfxFont->last - fontSmall.gfxFont->first + 1) * sizeof(GFXglyph));
    for (const auto &entry : gEpubArchiveCache.entries) {
        bytes(entry.path.data(), entry.path.size());
        bytes(&entry.crc, sizeof(entry.crc));
        bytes(&entry.uncompressedSize, sizeof(entry.uncompressedSize));
    }
    return hash;
}

std::string InkHUD::BookReaderApplet::pageIndexLabel() const
{
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : bookPath)
        hash = (hash ^ c) * 1099511628211ULL;
    return "reader-pages-" + std::to_string(hash);
}

void InkHUD::BookReaderApplet::recordCurrentPartPages()
{
    if (!pagedBookMode || pagedPartIndex < 0 || static_cast<size_t>(pagedPartIndex) >= partPageCounts.size() ||
        openFileTruncated || deferredWrapExpansion || deferredPartLoad.partIndex == pagedPartIndex || wrapDirty)
        return;
    partPageCounts[pagedPartIndex] = readerPageCount(wrappedLines.size(), linesPerPage);
    updateContentsLines(pagedPartIndex);
}

void InkHUD::BookReaderApplet::ensurePageIndex()
{
    if (!pagedBookMode || !fileLoaded)
        return;
    if (pageNavigation) {
        if (backgroundReaderWorkPending && !concurrency::OSThread::enabled) {
            concurrency::OSThread::enabled = true;
            setIntervalFromNow(500);
        }
        return;
    }
    if (pageIndexWidth != width() || pageIndexHeight != height() || partPageCounts.size() != pagedParts.size()) {
        resetPageIndex();
        pageIndexWidth = width();
        pageIndexHeight = height();
        partPageCounts.assign(pagedParts.size(), 0);
        PageIndexCache cache;
        if (FlashData<PageIndexCache>::load(&cache, pageIndexLabel().c_str()) && cache.version == PageIndexCache::VERSION &&
            cache.fingerprint == pageIndexFingerprint() && cache.width == width() && cache.height == height() &&
            cache.textSize == 0 && cache.count == pagedParts.size() && cache.count <= cache.pages.size() &&
            std::all_of(cache.pages.begin(), cache.pages.begin() + cache.count, [](uint16_t count) { return count > 0; })) {
            std::copy_n(cache.pages.begin(), cache.count, partPageCounts.begin());
            pageIndexSaved = true;
        }
    }
    recordCurrentPartPages();
    if ((!pageIndexSaved || backgroundReaderWorkPending) && !concurrency::OSThread::enabled) {
        concurrency::OSThread::enabled = true;
        setIntervalFromNow(500);
    }
}

int32_t InkHUD::BookReaderApplet::runOnce()
{
    if (!isActive() || !isForeground() || !fileLoaded || mode != Mode::READER) {
        imageInteractionPending = false;
        return concurrency::OSThread::disable();
    }
    if (imageInteractionPending) {
        if (readerInputPending() || !Throttle::hasElapsed(lastReaderInputMs, 150U))
            return 25;
        imageInteractionPending = false;
        BR_PERF_LOG("image interaction settled zoom=%u pan=%d,%d", imageZoomPermille, imagePanX, imagePanY);
        requestUpdate(Drivers::EInk::UpdateTypes::FAST, !imagePanOnlyRender || (wantsToRender() && wantsFullRender()));
        return 100;
    }
    if (!pagedBookMode)
        return concurrency::OSThread::disable();
    if (readerInputPending()) {
        lastReaderInputMs = Time::skipZero(Time::getMillis());
        return 100;
    }
    uint32_t quietMs = 1500;
#if INKHUD_BOOKREADER_HAS_SD && INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES && INKHUD_BOOKREADER_USE_JPEGDEC
    if (deferredNextPartPrefetch && !deferredWrapExpansion && deferredPartLoad.partIndex < 0)
        quietMs = 100;
#endif
    if (deferInlineImagesUntilNextRender || backgroundReaderWorkArmed ||
        (lastReaderInputMs && !Throttle::hasElapsed(lastReaderInputMs, quietMs)) ||
        (lastReaderRenderMs && !Throttle::hasElapsed(lastReaderRenderMs, quietMs)))
        return 100;
    if (backgroundReaderWorkPending) {
        const bool layoutWork = deferredWrapExpansion || deferredPartLoad.partIndex >= 0;
#if INKHUD_BOOKREADER_HAS_SD && INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES && INKHUD_BOOKREADER_USE_JPEGDEC
        if (!layoutWork && gJpegPrefetchJob && !gJpegPrefetchJob->done.load())
            return 100;
#endif
        if (runBackgroundReaderPass() && layoutWork)
            requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        return 25;
    }
    if (pageNavigation || pageIndexSaved)
        return concurrency::OSThread::disable();
    while (pageIndexNext < partPageCounts.size() && partPageCounts[pageIndexNext] != 0) {
        pageIndexText.clear();
        pageIndexImageOffset = 0;
        ++pageIndexNext;
    }
    if (pageIndexNext == partPageCounts.size()) {
        const bool complete = std::all_of(partPageCounts.begin(), partPageCounts.end(), [](int32_t count) { return count > 0; });
        if (complete && !pageIndexSaved && partPageCounts.size() <= 256 &&
            std::all_of(partPageCounts.begin(), partPageCounts.end(), [](int32_t count) { return count <= UINT16_MAX; })) {
            PageIndexCache cache;
            cache.fingerprint = pageIndexFingerprint();
            cache.width = pageIndexWidth;
            cache.height = pageIndexHeight;
            cache.textSize = 0;
            cache.count = partPageCounts.size();
            std::copy(partPageCounts.begin(), partPageCounts.end(), cache.pages.begin());
            bool canSave = true;
#ifdef ARCH_ESP32
            const size_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            const size_t internalLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            BR_PERF_LOG("pagination save memory internalFree=%u internalLargest=%u psramFree=%u",
                        static_cast<unsigned>(internalFree), static_cast<unsigned>(internalLargest),
                        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
            canSave = internalFree >= 8192 && internalLargest >= 4096;
#endif
            if (canSave)
                FlashData<PageIndexCache>::save(&cache, pageIndexLabel().c_str());
            else
                LOG_WARN("Book reader: skipping optional page cache save; internal memory low");
        }
        pageIndexSaved = true;
        BR_PERF_LOG("pagination finished parts=%u pages=%lu complete=%u", static_cast<unsigned>(partPageCounts.size()),
                    static_cast<unsigned long>(readerPagePrefix(partPageCounts, partPageCounts.size())), complete);
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        return concurrency::OSThread::disable();
    }

    // Pagination shares the reader's exact wrapper, but never replaces the visible page.
    const size_t part = pageIndexNext;
    [[maybe_unused]] const uint32_t started = millis();
    indexingReaderPages = true;
    bool truncated = false;
    if (pageIndexText.empty()) {
        if (!loadPartText(pagedParts[part], maxReadBytesForFormat(pagedParts[part].format), pageIndexText, truncated) ||
            truncated) {
            partPageCounts[part] = -1;
            ++pageIndexNext;
            pageIndexText.clear();
            pageIndexImageOffset = 0;
            indexingReaderPages = false;
            return 500;
        }
        if (part == 0 && useEpubCoverForFirstPart)
            pageIndexText = makeImageMarkerLine(epubCoverImagePath);
    }
    // Warm one image per scheduler pass, so a chapter cannot monopolize touch input.
    if (warmNextImageHeader(pageIndexText, pageIndexImageOffset)) {
        indexingReaderPages = false;
        return 25;
    }
    auto savedText = std::move(displayText);
    auto savedLines = std::move(wrappedLines);
    auto savedStyles = std::move(wrappedLineStyles);
    auto savedOffsets = std::move(chapterOffsets);
    auto savedStarts = std::move(chapterStartLines);
    const auto savedFont = getFont();
    const bool savedDeferred = deferredWrapExpansion;
    const int32_t savedLinesPerPage = linesPerPage;
    displayText = std::move(pageIndexText);
    pageIndexText.clear();
    pageIndexImageOffset = 0;
    ++pageIndexNext;
    chapterOffsets = {0};
    for (const auto &entry : contents)
        if (entry.part == static_cast<int32_t>(part) && entry.offset != std::string::npos)
            chapterOffsets.push_back(entry.offset);
    std::sort(chapterOffsets.begin(), chapterOffsets.end());
    chapterOffsets.erase(std::unique(chapterOffsets.begin(), chapterOffsets.end()), chapterOffsets.end());
    const int32_t contentHeight = std::max<int32_t>(1, pageIndexHeight - getHeaderHeight() -
                                                           ReaderToolbar::progressFooterHeight(fontSmall.lineHeight()) - 3);
    const int32_t step = std::max<int32_t>(1, contentHeight / readingFont().lineHeight());
    linesPerPage = step;
    wrapDisplayText(pageIndexWidth);
    partPageCounts[part] = readerPageCount(wrappedLines.size(), step);
    updateContentsLines(part);
    displayText = std::move(savedText);
    wrappedLines = std::move(savedLines);
    wrappedLineStyles = std::move(savedStyles);
    chapterOffsets = std::move(savedOffsets);
    chapterStartLines = std::move(savedStarts);
    deferredWrapExpansion = savedDeferred;
    linesPerPage = savedLinesPerPage;
    setFont(savedFont);
    indexingReaderPages = false;
    BR_PERF_LOG("pagination part=%u pages=%ld ms=%lu", static_cast<unsigned>(part), static_cast<long>(partPageCounts[part]),
                static_cast<unsigned long>(millis() - started));
    return 500;
}

void InkHUD::BookReaderApplet::installContents(const std::vector<ContentsEntry> &outline)
{
    BR_LOAD_SCOPE("contents-install");
    std::vector<std::string> partKeys;
    partKeys.reserve(pagedParts.size());
    for (const auto &part : pagedParts)
        partKeys.push_back(
            normalizeArchivePath(isEpubVirtualPath(part.path) ? epubVirtualPathToEntryPath(part.path) : part.path));
    contents.clear();
    for (const auto &entry : outline) {
        const auto *zipEntry = findZipEntryByKey(gEpubArchiveCache.entries, entry.path);
        const std::string key = zipEntry ? zipEntry->path : normalizeArchivePath(entry.path);
        for (size_t part = 0; part < pagedParts.size(); ++part) {
            if (key == partKeys[part]) {
                contents.push_back(entry);
                contents.back().path = key;
                contents.back().part = part;
                if (entry.fragment.empty()) {
                    contents.back().offset = 0;
                    contents.back().line = 0;
                }
                break;
            }
        }
    }
    for (size_t part = 0; part < pagedParts.size(); ++part) {
        if (std::none_of(contents.begin(), contents.end(),
                         [&](const auto &entry) { return entry.part == static_cast<int32_t>(part); })) {
            ContentsEntry entry;
            entry.title = pageNavigation ? "Page " + std::to_string(part + 1) : titleFromPath(pagedParts[part].name);
            entry.path = isEpubVirtualPath(pagedParts[part].path) ? epubVirtualPathToEntryPath(pagedParts[part].path)
                                                                  : pagedParts[part].path;
            entry.part = part;
            entry.offset = 0;
            entry.line = 0;
            auto next = std::find_if(contents.begin(), contents.end(), [&](const auto &item) { return item.part > entry.part; });
            contents.insert(next, std::move(entry));
        }
    }
}

void InkHUD::BookReaderApplet::resolveContentsOffsets(const std::string &path, const std::string &raw, std::string &text,
                                                      bool *incompleteTable)
{
    auto readerFont = readingFont();
    std::vector<std::pair<std::string, size_t>> anchors;
    const std::string key = normalizeArchivePath(isEpubVirtualPath(path) ? epubVirtualPathToEntryPath(path) : path);
    for (const auto &entry : contents)
        if (normalizeArchivePath(entry.path) == key && !entry.fragment.empty())
            anchors.emplace_back(entry.fragment, std::string::npos);
    if (anchors.empty() || formatForPath(path) != BookFormat::HTML_LIKE) {
        text = readerFont.decodeUTF8(extractDisplayText(raw, formatForPath(path), path, incompleteTable));
        return;
    }
    const std::string normalized = stripHtmlLike(raw, parentDir(path), &anchors, incompleteTable);
    if (incompleteTable && *incompleteTable)
        return;
    const std::string collapsed = collapseImageOnlyPageToSingleMarker(normalized);
    std::vector<size_t> offsets;
    for (const auto &anchor : anchors)
        if (anchor.second != std::string::npos)
            offsets.push_back(std::min(anchor.second, normalized.size()));
    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
    std::vector<size_t> decodedOffsets;
    size_t start = 0;
    text.clear();
    text.reserve(collapsed.size());
    if (collapsed == normalized) {
        for (const size_t offset : offsets) {
            text += readerFont.decodeUTF8(normalized.substr(start, offset - start));
            decodedOffsets.push_back(text.size());
            start = offset;
        }
        text += readerFont.decodeUTF8(normalized.substr(start));
    } else {
        text = readerFont.decodeUTF8(collapsed);
    }
    for (auto &entry : contents) {
        if (normalizeArchivePath(entry.path) != key || entry.fragment.empty())
            continue;
        const auto anchor =
            std::find_if(anchors.begin(), anchors.end(), [&](const auto &a) { return a.first == entry.fragment; });
        if (anchor != anchors.end() && anchor->second != std::string::npos) {
            const size_t index =
                std::lower_bound(offsets.begin(), offsets.end(), std::min(anchor->second, normalized.size())) - offsets.begin();
            entry.offset = collapsed != normalized ? 0 : decodedOffsets[index];
        }
    }
}

void InkHUD::BookReaderApplet::updateContentsLines(int32_t part)
{
    for (auto &entry : contents) {
        if (entry.part != part || entry.offset == std::string::npos)
            continue;
        const auto pos = std::lower_bound(chapterOffsets.begin(), chapterOffsets.end(), entry.offset);
        if (pos != chapterOffsets.end() && *pos == entry.offset) {
            const size_t index = pos - chapterOffsets.begin();
            if (index < chapterStartLines.size())
                entry.line = chapterStartLines[index];
        }
    }
}

std::string InkHUD::BookReaderApplet::readingProgress() const
{
    if (pagedBookMode && pageNavigation && !pagedParts.empty())
        return "Pg " + std::to_string(pagedPartIndex + 1) + "/" + std::to_string(pagedParts.size());
    const uint32_t localPage = scrollLine / std::max<int32_t>(1, linesPerPage) + 1;
    if (!pagedBookMode)
        return "Pg " + std::to_string(localPage) + "/" +
               (openFileTruncated || deferredWrapExpansion ? "?"
                                                           : std::to_string(readerPageCount(wrappedLines.size(), linesPerPage)));
    const size_t part = std::max<int32_t>(0, pagedPartIndex);
    const uint32_t prefix = readerPagePrefix(partPageCounts, part);
    const uint32_t total = readerPagePrefix(partPageCounts, pagedParts.size());
    return "Pg " + (part == 0 || prefix > 0 ? std::to_string(prefix + localPage) : "?") + "/" +
           (total > 0 && partPageCounts.size() == pagedParts.size() ? std::to_string(total) : "?");
}

std::string InkHUD::BookReaderApplet::readingContext() const
{
    if (pageNavigation || contents.empty())
        return "";
    const size_t current = std::clamp<int32_t>(getCurrentChapterIndex(), 0, contents.size() - 1);
    size_t root = current;
    while (root > 0 && contents[root].depth > 0)
        --root;
    return contents[root].title;
}

void InkHUD::BookReaderApplet::renderReadingProgress()
{
    setFont(fontSmall);
    const std::string progress = readingProgress();
    std::string context = parse(readingContext());
    for (char &c : context)
        if (c == '\n' || c == '\r' || c == '\t')
            c = ' ';
    const int32_t available = std::max<int32_t>(1, width() - 16);
    const std::string prefix = progress + "  |  ";
    if (!context.empty() && getTextWidth(prefix + context) > available) {
        while (!context.empty() && getTextWidth(prefix + context + "...") > available) {
            context.pop_back();
        }
        if (getTextWidth(prefix + "...") <= available)
            context += "...";
    }
    setCrop(8, height() - ReaderToolbar::progressFooterHeight(fontSmall.lineHeight()), width() > 16 ? width() - 16 : 1,
            ReaderToolbar::progressFooterHeight(fontSmall.lineHeight()));
    printAt(width() / 2, height() - ReaderToolbar::progressBottomInset, context.empty() ? progress : prefix + context, CENTER,
            BOTTOM);
    resetCrop();
}

void InkHUD::BookReaderApplet::onActivate()
{
    setInputsSubscribed(BUTTON_SHORT | BUTTON_LONG | EXIT_SHORT | NAV_UP | NAV_DOWN | NAV_LEFT | NAV_RIGHT, true);
    mode = Mode::BROWSER;
    initialized = false;
    initialLoadArmed = false;
    pendingOpenIndex = -1;
    pendingChapterIndex = -1;
    pendingPageDelta = 0;
    openingSplashRendered = false;
    requestUpdate(Drivers::EInk::UpdateTypes::FULL);
}

void InkHUD::BookReaderApplet::onDeactivate()
{
    saveReadingPosition();
    resetSessionState();
}

void InkHUD::BookReaderApplet::onBackground()
{
    concurrency::OSThread::disable();
#if INKHUD_BOOKREADER_HAS_SD && INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES && INKHUD_BOOKREADER_USE_JPEGDEC
    clearJpegPrefetchJob();
#endif
    saveReadingPosition();
}

void InkHUD::BookReaderApplet::onShutdown()
{
#if INKHUD_BOOKREADER_HAS_SD && INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES && INKHUD_BOOKREADER_USE_JPEGDEC
    clearJpegPrefetchJob();
#endif
    saveReadingPosition();
}

InkHUD::AppletFont InkHUD::BookReaderApplet::readingFont()
{
    return fontSmall;
}

void InkHUD::BookReaderApplet::saveReadingPosition()
{
    if (!fileLoaded || bookPath.empty() || bookPath.size() >= sizeof(readingPosition.path)) {
        return;
    }
    readingPosition.part = -1;
    readingPosition.line = 0;
    readingPosition.textSize = 0;
    std::strcpy(readingPosition.path, bookPath.c_str());
    uint64_t key = 14695981039346656037ULL;
    for (unsigned char c : bookPath) {
        key = (key ^ c) * 1099511628211ULL;
    }
    const std::string label = "reader-" + std::to_string(key);
    FlashData<ReadingPosition>::save(&readingPosition, label.c_str());
}

void InkHUD::BookReaderApplet::restoreLocation(int32_t part, int32_t line)
{
    closeExpandedImage();
    if (pagedBookMode && part >= 0 && part < static_cast<int32_t>(pagedParts.size()) &&
        (part != pagedPartIndex || (line > 0 && deferredPartLoad.partIndex == part))) {
        if (!loadPagedPart(part, false)) {
            return;
        }
    }
    wrapDirty = true;
    rewrapIfNeeded(width(), std::max<int32_t>(1, height() - getHeaderHeight() -
                                                     ReaderToolbar::progressFooterHeight(fontSmall.lineHeight()) - 3));
    scrollLine = std::clamp<int32_t>(line, 0, readerLastPageStart(wrappedLines.size(), linesPerPage));
    resetImageView();
}

void InkHUD::BookReaderApplet::loadReadingPosition()
{
    readingPosition = ReadingPosition{};
    if (bookPath.empty() || bookPath.size() >= sizeof(readingPosition.path)) {
        return;
    }
    uint64_t key = 14695981039346656037ULL;
    for (unsigned char c : bookPath) {
        key = (key ^ c) * 1099511628211ULL;
    }
    const std::string label = "reader-" + std::to_string(key);
    const bool valid =
        FlashData<ReadingPosition>::load(&readingPosition, label.c_str()) && readingPosition.version == 1 &&
        std::strncmp(readingPosition.path, bookPath.c_str(), sizeof(readingPosition.path)) == 0 && readingPosition.textSize < 3 &&
        readingPosition.bookmarkTextSize < 3 && readingPosition.line >= 0 &&
        (readingPosition.bookmarkLine < 0 || !pagedBookMode ||
         (readingPosition.bookmarkPart >= 0 && readingPosition.bookmarkPart < static_cast<int32_t>(pagedParts.size())));
    if (!valid) {
        readingPosition = ReadingPosition{};
    }
}

void InkHUD::BookReaderApplet::resetSessionState()
{
    resetPageIndex();
    contents.clear();
    gExternalCssCaches.clear();
    mode = Mode::BROWSER;
    initialized = false;
    initialLoadArmed = false;
    currentDir = BOOKS_DIR;
    entries.clear();
    clearBackgroundReaderState();
    closeCachedEpubFile();
    gEpubArchiveCache.clear();
    gImageProbeCache.clear();
    browserCursorShown = false;
    hideTouchBrowserSelectionHighlight = false;
    pagedParts.clear();
    pagedPartIndex = -1;
    pagedBookMode = false;
    prefetchedNextPart.clear();
    wrappedLines.clear();
    wrappedLineStyles.clear();
    chapterOffsets.clear();
    chapterStartLines.clear();
    chapterPickerSelected = 0;
    chapterPickerFirstVisible = 0;
    chapterCursorShown = true;
    hideTouchChapterSelectionHighlight = false;
    deferInlineImagesUntilNextRender = false;
    closeExpandedImage();
    resetImageView();
    clearRenderedImageCaches();
    pendingOpenIndex = -1;
    pendingChapterIndex = -1;
    pendingPageDelta = 0;
    openingSplashRendered = false;
    displayText.clear();
    fileLoaded = false;
    status.clear();
    selectedIndex = 0;
    firstVisibleIndex = 0;
    openFilePath.clear();
    bookPath.clear();
    epubCoverImagePath.clear();
    useEpubCoverForFirstPart = false;
    pageNavigation = false;
    openFileTruncated = false;
    scrollLine = 0;
    linesPerPage = 1;
    wrapWidth = 0;
    wrapDirty = true;
}

void InkHUD::BookReaderApplet::clearBackgroundReaderState()
{
    deferredPartLoad.clear();
    deferredNextPartPrefetch = false;
    prefetchImageIndex = 0;
    deferredWrapExpansion = false;
    backgroundReaderWorkPending = false;
    backgroundReaderWorkArmed = false;
    deferredWrapImageOffset = 0;
}

void InkHUD::BookReaderApplet::updateBackgroundReaderPending()
{
    backgroundReaderWorkPending = (deferredPartLoad.partIndex >= 0) || deferredWrapExpansion || deferredNextPartPrefetch;
    if (!backgroundReaderWorkPending) {
        backgroundReaderWorkArmed = false;
    }
}

void InkHUD::BookReaderApplet::queueDeferredPartExpansion(int32_t partIndex)
{
    deferredPartLoad.partIndex = partIndex;
    backgroundReaderWorkArmed = true;
    updateBackgroundReaderPending();
}

void InkHUD::BookReaderApplet::queueDeferredWrapExpansion()
{
    deferredWrapExpansion = true;
    deferredWrapImageOffset = 0;
    backgroundReaderWorkArmed = true;
    updateBackgroundReaderPending();
}

void InkHUD::BookReaderApplet::queueDeferredNextPartPrefetch()
{
    if (!pagedBookMode || pagedPartIndex < 0) {
        return;
    }
    deferredNextPartPrefetch = true;
    prefetchImageIndex = 0;
    backgroundReaderWorkArmed = true;
    updateBackgroundReaderPending();
}

bool InkHUD::BookReaderApplet::runBackgroundReaderPass()
{
    BR_LOAD_SCOPE("deferred-work");
    BR_PERF_LOG("deferred part=%ld wrap=%u prefetch=%u", static_cast<long>(deferredPartLoad.partIndex),
                static_cast<unsigned>(deferredWrapExpansion), static_cast<unsigned>(deferredNextPartPrefetch));
    if (deferredPartLoad.partIndex >= 0) {
        const int32_t targetPartIndex = deferredPartLoad.partIndex;
        const int32_t savedScrollLine = scrollLine;
        deferredPartLoad.clear();
        updateBackgroundReaderPending();
        if (pagedBookMode && pagedPartIndex == targetPartIndex) {
            if (!loadPagedPart(targetPartIndex, false)) {
                queueDeferredPartExpansion(targetPartIndex);
                backgroundReaderWorkArmed = false;
                return false;
            }
            scrollLine = std::clamp<int32_t>(savedScrollLine, 0, readerLastPageStart(wrappedLines.size(), linesPerPage));
            return true;
        }
        return true;
    }

    if (deferredWrapExpansion) {
        if (pagedBookMode && warmNextImageHeader(displayText, deferredWrapImageOffset))
            return false;
        deferredWrapExpansion = false;
        wrapDirty = true;
        const uint16_t headerBottom = getHeaderHeight();
        const uint16_t footerHeight = ReaderToolbar::progressFooterHeight(fontSmall.lineHeight());
        const uint16_t contentTop = headerBottom + 2;
        const uint16_t contentHeight = (height() > footerHeight && (height() - footerHeight) > contentTop)
                                           ? static_cast<uint16_t>(height() - footerHeight - contentTop - 1)
                                           : 0;
        rewrapIfNeeded(width(), contentHeight, false);
        updateBackgroundReaderPending();
        return true;
    }

    if (deferredNextPartPrefetch) {
        deferredNextPartPrefetch = false;
        prefetchNextPartIfPossible();
        updateBackgroundReaderPending();
        return true;
    }

    updateBackgroundReaderPending();
    return false;
}

bool InkHUD::BookReaderApplet::ensureSDReady()
{
#if !INKHUD_BOOKREADER_HAS_SD
    status = "This build has no SD support.";
    return false;
#else
    {
        // SD and LoRa can share SPI on this target, so every SD status check must
        // be locked too.
        concurrency::LockGuard guard(spiLock);
        if (SD.cardType() != CARD_NONE) {
            return true;
        }
    }

    status = "SD card not ready.";
    return false;
#endif
}

bool InkHUD::BookReaderApplet::loadDirectory(const std::string &dir)
{
    BR_LOAD_SCOPE("library-scan");
    BR_PERF_LOG("library path=%s", dir.c_str());
#if !INKHUD_BOOKREADER_HAS_SD
    currentDir = "/";
    entries.clear();
    selectedIndex = 0;
    firstVisibleIndex = 0;
    return false;
#else
    if (!ensureSDReady()) {
        entries.clear();
        selectedIndex = 0;
        firstVisibleIndex = 0;
        return false;
    }

    const std::string target = normalizeDir(dir);
    std::vector<BrowserEntry> found;
    found.reserve(64);

    {
        concurrency::LockGuard guard(spiLock);
        File root = SD.open(target.c_str(), FILE_READ);
        if (!root || !root.isDirectory()) {
            status = "Can't open folder: " + target;
            entries.clear();
            selectedIndex = 0;
            firstVisibleIndex = 0;
            return false;
        }

        File file = root.openNextFile();
        while (file) {
            std::string path = file.name();
            if (path.empty()) {
                file.close();
                file = root.openNextFile();
                continue;
            }
            if (path[0] != '/') {
                path = joinPath(target, path);
            }

            BrowserEntry entry;
            entry.path = path;
            entry.name = basenameOf(path);
            entry.isDir = file.isDirectory();

            if (!entry.isDir) {
                entry.size = static_cast<uint32_t>(file.size());
                if (hasSupportedBookExtension(path)) {
                    entry.format = BookFormat::EPUB_CONTAINER;
                } else {
                    file.close();
                    file = root.openNextFile();
                    continue;
                }
            }

            if (!entry.name.empty() && entry.name != "." && entry.name != "..") {
                found.push_back(entry);
                if (found.size() >= MAX_BROWSER_ENTRIES) {
                    file.close();
                    break;
                }
            }

            file.close();
            file = root.openNextFile();
        }
        root.close();
    }

    std::sort(found.begin(), found.end(), [](const BrowserEntry &a, const BrowserEntry &b) {
        if (a.isDir != b.isDir) {
            return a.isDir && !b.isDir;
        }
        return std::lexicographical_compare(
            a.name.begin(), a.name.end(), b.name.begin(), b.name.end(),
            [](unsigned char left, unsigned char right) { return std::tolower(left) < std::tolower(right); });
    });
    BR_PERF_LOG("library scan finished path=%s entries=%u", target.c_str(), static_cast<unsigned>(found.size()));

    currentDir = target;
    entries = std::move(found);
    selectedIndex = entries.empty() ? -1 : 0;
    firstVisibleIndex = 0;
    const bool touch = useTouchFriendlyBookReaderLayout(inkhud);
    browserCursorShown = !entries.empty() && !touch;
    hideTouchBrowserSelectionHighlight = !entries.empty() && touch;

    if (entries.empty()) {
        status = "No folders/files here.\nUse /books for book files.";
    } else {
        status.clear();
    }
    return true;
#endif
}

bool InkHUD::BookReaderApplet::ensureInitialDirectory()
{
    if (initialized) {
        return true;
    }
    initialized = true;
    mode = Mode::BROWSER;
    if (loadDirectory(BOOKS_DIR)) {
        return true;
    }
    return loadDirectory("/");
}

BOOKREADER_NOINLINE bool InkHUD::BookReaderApplet::loadEpubContainerAsBook(const BrowserEntry &entry)
{
    BR_LOAD_SCOPE("epub-open");
#if !INKHUD_BOOKREADER_HAS_SD
    (void)entry;
    return false;
#else
    [[maybe_unused]] const uint32_t tStartMs = millis();
    if (!ensureSDReady()) {
        return false;
    }

    if (!ensureEpubArchiveCached(entry.path)) {
        status = "EPUB archive index failed.";
        mode = Mode::READER;
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        return false;
    }
    BR_PERF_LOG("epub archive ready entries=%u elapsedMs=%lu", static_cast<unsigned>(gEpubArchiveCache.entries.size()),
                static_cast<unsigned long>(millis() - tStartMs));

    std::string containerXml;
    bool truncated = false;
    if (!readEpubEntryLimited("meta-inf/container.xml", MAX_OPF_PARSE_BYTES, containerXml, truncated)) {
        status = "Invalid EPUB: no container.xml";
        mode = Mode::READER;
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        return false;
    }

    std::string opfEntryPath;
    if (!parseEpubRootfilePath(containerXml, opfEntryPath)) {
        opfEntryPath = pickFallbackOpfFromZip(gEpubArchiveCache.entries);
    }
    if (opfEntryPath.empty()) {
        status = "Invalid EPUB: no OPF package file";
        mode = Mode::READER;
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        return false;
    }

    std::string opfText;
    if (!readEpubEntryLimited(opfEntryPath, MAX_OPF_PARSE_BYTES, opfText, truncated)) {
        status = "Can't read EPUB OPF metadata";
        mode = Mode::READER;
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        return false;
    }

    const std::string opfDir = parentDir("/" + normalizeArchivePath(opfEntryPath));
    pageNavigation = opfUsesPageNavigation(opfText);
    const std::string coverPath = parseOpfCoverImagePath(opfText, opfDir);
    if (!coverPath.empty() && findZipEntryByKey(gEpubArchiveCache.entries, coverPath)) {
        epubCoverImagePath = toEpubVirtualPath(coverPath);
        BR_PERF_LOG("EPUB declared cover: %s", epubCoverImagePath.c_str());
    }
    std::vector<std::string> spineOrderAbs;
    parseOpfSpineOrder(opfText, opfDir, spineOrderAbs);
    BR_PERF_LOG("epub metadata ready spine=%u elapsedMs=%lu", static_cast<unsigned>(spineOrderAbs.size()),
                static_cast<unsigned long>(millis() - tStartMs));

    std::vector<std::string> tocCandidates;
    parseOpfTocCandidates(opfText, opfDir, tocCandidates);
    const std::string preferredToc = pickPreferredTocPath(tocCandidates);
    std::vector<ContentsEntry> outline;
    if (!preferredToc.empty()) {
        std::string tocText;
        const std::string tocEntryPath = normalizeArchivePath(preferredToc);
        if (!tocEntryPath.empty() && readEpubEntryLimited(tocEntryPath, MAX_TOC_PARSE_BYTES, tocText, truncated)) {
            const std::string lowerToc = toLowerCopy(tocEntryPath);
            const std::string tocDir = parentDir("/" + tocEntryPath);
            parseContentsNavigation(tocText, tocDir, lowerToc.size() >= 4U && lowerToc.rfind(".ncx") == lowerToc.size() - 4U,
                                    outline);
        }
    }

    std::vector<BrowserEntry> parts;
    BR_PERF_LOG("epub toc ready entries=%u elapsedMs=%lu", static_cast<unsigned>(outline.size()),
                static_cast<unsigned long>(millis() - tStartMs));
    parts.reserve(gEpubArchiveCache.entries.size());
    std::vector<std::string> addedKeys;
    addedKeys.reserve(256U);

    auto appendPartIfValid = [&](const std::string &absOrRelPath) {
        const ZipCentralEntry *zipEntry = findZipEntryByKey(gEpubArchiveCache.entries, absOrRelPath);
        if (!zipEntry || std::find(addedKeys.begin(), addedKeys.end(), zipEntry->path) != addedKeys.end()) {
            return;
        }
        const std::string lowerPath = toLowerCopy(zipEntry->path);
        if (lowerPath.rfind("meta-inf/", 0) == 0 ||
            (lowerPath.size() >= 4U && lowerPath.rfind(".opf") == lowerPath.size() - 4U) ||
            (lowerPath.size() >= 4U && lowerPath.rfind(".ncx") == lowerPath.size() - 4U) ||
            (lowerPath.size() >= 4U && lowerPath.rfind(".css") == lowerPath.size() - 4U)) {
            return;
        }
        if (!hasSupportedContinuousChapterExtension(zipEntry->path)) {
            return;
        }
        const BookFormat format = formatForPath(zipEntry->path);
        BrowserEntry part;
        part.path = toEpubVirtualPath(zipEntry->path);
        part.name = basenameOf(zipEntry->path);
        part.isDir = false;
        part.size = zipEntry->uncompressedSize;
        part.format = format;
        parts.push_back(std::move(part));
        addedKeys.push_back(zipEntry->path);
    };

    for (const std::string &spineAbsPath : spineOrderAbs) {
        appendPartIfValid(spineAbsPath);
    }
    if (parts.empty()) {
        for (const ZipCentralEntry &zipEntry : gEpubArchiveCache.entries) {
            appendPartIfValid(zipEntry.path);
        }
    }

    pagedBookMode = true;

    if (parts.empty()) {
        status = "No readable chapters in EPUB.";
        mode = Mode::READER;
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        return false;
    }

    pagedParts = std::move(parts);
    installContents(outline);
    chapterOffsets.push_back(0);
    chapterStartLines.push_back(0);
    chapterPickerSelected = 0;
    chapterPickerFirstVisible = 0;

    loadReadingPosition();
    const int32_t initialPart = readingPosition.bookmarkLine >= 0 ? readingPosition.bookmarkPart : 0;
    const bool firstPage = readingPosition.bookmarkLine <= 0;
    const size_t initialReadLimit =
        firstPage ? std::min(maxReadBytesForFormat(pagedParts[initialPart].format), INITIAL_PARTIAL_BYTES) : 0U;
    BR_PERF_LOG("epub initial chapter BEGIN part=%ld parts=%u elapsedMs=%lu", static_cast<long>(initialPart),
                static_cast<unsigned>(pagedParts.size()), static_cast<unsigned long>(millis() - tStartMs));
    if (!loadPagedPart(initialPart, false, initialReadLimit, firstPage) &&
        (initialPart == 0 || !loadPagedPart(0, false, INITIAL_PARTIAL_BYTES, true))) {
        status = "Can't open EPUB chapter";
        mode = Mode::READER;
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        return false;
    }
    if (readingPosition.bookmarkLine > 0 && readingPosition.bookmarkPart == pagedPartIndex) {
        scrollLine = std::clamp<int32_t>(readingPosition.bookmarkLine, 0, readerLastPageStart(wrappedLines.size(), linesPerPage));
        chapterPickerSelected = getCurrentChapterIndex();
    }

    mode = Mode::READER;
    status.clear();
    BR_PERF_LOG("loadEpubContainerAsBook done path=%s chapters=%u totalMs=%lu", entry.path.c_str(),
                static_cast<unsigned>(pagedParts.size()), static_cast<unsigned long>(millis() - tStartMs));
    return true;
#endif
}

BOOKREADER_NOINLINE bool InkHUD::BookReaderApplet::loadFile(const BrowserEntry &entry)
{
    const bool reuseEpubCaches = fileLoaded && bookPath == entry.path && entry.format == BookFormat::EPUB_CONTAINER &&
                                 gEpubArchiveCache.ready && gEpubArchiveCache.epubPath == entry.path;
    if (!reuseEpubCaches) {
        gExternalCssCaches.clear();
        clearRenderedImageCaches();
        gImageProbeCache.clear();
    }
#if INKHUD_BOOKREADER_HAS_SD && INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES && INKHUD_BOOKREADER_USE_JPEGDEC
    else {
        clearJpegPrefetchJob();
    }
#endif
    BR_PERF_LOG("book image caches reused=%u path=%s", reuseEpubCaches, entry.path.c_str());
    resetPageIndex();
    contents.clear();
    pageNavigation = false;
    bookPath = entry.path;
    epubCoverImagePath.clear();
    useEpubCoverForFirstPart = false;
    closeExpandedImage();
    resetImageView();
#if !INKHUD_BOOKREADER_HAS_SD
    (void)entry;
    return false;
#else
    pagedBookMode = false;
    pagedPartIndex = -1;
    pagedParts.clear();
    prefetchedNextPart.clear();
    clearBackgroundReaderState();

    openFilePath = entry.path;
    openFileTruncated = false;
    fileLoaded = false;
    deferInlineImagesUntilNextRender = false;
    wrappedLines.clear();
    wrappedLineStyles.clear();
    chapterOffsets.clear();
    chapterStartLines.clear();
    displayText.clear();
    scrollLine = 0;
    wrapDirty = true;

    return loadEpubContainerAsBook(entry);
#endif
}

BOOKREADER_NOINLINE bool InkHUD::BookReaderApplet::loadPartText(const BrowserEntry &part, size_t maxBytes, std::string &out,
                                                                bool &truncatedOut, bool *needsExpansion)
{
    if (needsExpansion)
        *needsExpansion = false;
#if !INKHUD_BOOKREADER_HAS_SD
    (void)part;
    (void)maxBytes;
    out.clear();
    truncatedOut = false;
    return false;
#else
    [[maybe_unused]] const uint32_t tPartStartMs = millis();
    out.clear();
    truncatedOut = false;
    if (!maxBytes || !isEpubVirtualPath(part.path))
        return false;

    std::string raw;
    [[maybe_unused]] const uint32_t tReadStartMs = millis();
    if (!readEpubVirtualTextFileLimited(part.path, maxBytes, raw, &truncatedOut)) {
        BR_PERF_LOG("loadPartText epub-read-failed path=%s totalMs=%lu", part.path.c_str(),
                    static_cast<unsigned long>(millis() - tPartStartMs));
        return false;
    }
    [[maybe_unused]] const uint32_t tParseStartMs = millis();
    bool incompleteTable = false;
    const size_t fullReadLimit = maxReadBytesForFormat(part.format);
    resolveContentsOffsets(part.path, raw, out, truncatedOut && maxBytes < fullReadLimit ? &incompleteTable : nullptr);
    if (incompleteTable) {
        // Do not show a flattened partial table that will reflow into a different page later.
        if (!readEpubVirtualTextFileLimited(part.path, fullReadLimit, raw, &truncatedOut))
            return false;
        resolveContentsOffsets(part.path, raw, out);
    }
    if (needsExpansion)
        *needsExpansion = truncatedOut && maxBytes < fullReadLimit && !incompleteTable;
    if (out.empty()) {
        out = "(empty)";
    }
    BR_PERF_LOG("loadPartText epub-done path=%s raw=%u parsed=%u parseMs=%lu "
                "totalMs=%lu",
                part.path.c_str(), static_cast<unsigned>(raw.size()), static_cast<unsigned>(out.size()),
                static_cast<unsigned long>(millis() - tParseStartMs), static_cast<unsigned long>(millis() - tPartStartMs));
    return true;
#endif
}
BOOKREADER_NOINLINE void InkHUD::BookReaderApplet::prefetchNextPartIfPossible()
{
    BR_LOAD_SCOPE("next-part-prefetch");
#if !INKHUD_BOOKREADER_HAS_SD
    return;
#else
    if (!pagedBookMode || pagedPartIndex < 0) {
        return;
    }

    const int32_t nextIndex = pagedPartIndex + 1;
    if (nextIndex < 0 || nextIndex >= static_cast<int32_t>(pagedParts.size())) {
        return;
    }
    if (prefetchedNextPart.partIndex == nextIndex) {
        deferredNextPartPrefetch = prefetchDecodedImages(prefetchedNextPart.text);
        return;
    }
    prefetchedNextPart.clear();
    prefetchImageIndex = 0;

    const BrowserEntry &nextPart = pagedParts[static_cast<size_t>(nextIndex)];
    const size_t readCap = std::min(maxReadBytesForFormat(nextPart.format), PREFETCH_MAX_FILE_BYTES);
    if (readCap == 0U || nextPart.size > readCap) {
        return;
    }

    std::string prefetchedText;
    bool prefetchedTruncated = false;
    if (!loadPartText(nextPart, readCap, prefetchedText, prefetchedTruncated)) {
        return;
    }

    if (prefetchedText.size() > PREFETCH_MAX_TEXT_BYTES) {
        return;
    }

    prefetchedNextPart.partIndex = nextIndex;
    prefetchedNextPart.path = nextPart.path;
    prefetchedNextPart.text = std::move(prefetchedText);
    prefetchedNextPart.truncated = prefetchedTruncated;
    deferredNextPartPrefetch = prefetchDecodedImages(prefetchedNextPart.text);
#endif
}

bool InkHUD::BookReaderApplet::prefetchDecodedImages(const std::string &text)
{
    // Prefetch image pages, not every illustration in a text chapter.
    std::string imagePath;
    std::vector<std::string> imagePaths;
    if (isImageMarkerLine(text, &imagePath))
        imagePaths.push_back(imagePath);
    else if (!isImageSequenceMarkerLine(text, &imagePaths))
        return false;
    if (prefetchImageIndex < imagePaths.size()) {
        const std::string &path = imagePaths[prefetchImageIndex];
        const ImageProbeResult probe = probeImageFile(path);
#if INKHUD_BOOKREADER_HAS_SD && INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES && INKHUD_BOOKREADER_USE_JPEGDEC
        if (gJpegPrefetchJob && gJpegPrefetchJob->path != path)
            clearJpegPrefetchJob();
        if (probe.valid && std::any_of(gDecodedImages.begin(), gDecodedImages.end(), [&](const auto &entry) {
                return entry.ready && entry.path == path && entry.width >= probe.width && entry.height >= probe.height;
            })) {
            ++prefetchImageIndex;
            return prefetchImageIndex < imagePaths.size();
        }
        if (probe.valid && gJpegPrefetchJob && gJpegPrefetchJob->path == path) {
            if (!gJpegPrefetchJob->done.load())
                return true;
            if (collectJpegPrefetch(path, false, displayText)) {
                ++prefetchImageIndex;
                return prefetchImageIndex < imagePaths.size();
            }
        } else if (probe.valid) {
            if (startJpegPrefetch(path, probe, displayText))
                return true;
            if (probe.type == ImageFileType::JPEG && isEpubVirtualPath(path)) {
                ++prefetchImageIndex;
                return prefetchImageIndex < imagePaths.size();
            }
        }
#endif
        bool interrupted = false;
        if (probe.valid)
            drawInlineImage(path, 0, 0, probe.width, probe.height, IMAGE_ZOOM_NORMAL, 0, 0, nullptr, false, true, false, -1,
                            &interrupted);
        if (interrupted) {
            lastReaderInputMs = Time::skipZero(Time::getMillis());
            return true;
        }
        ++prefetchImageIndex;
    }
    return prefetchImageIndex < imagePaths.size();
}

BOOKREADER_NOINLINE bool InkHUD::BookReaderApplet::loadPagedPart(int32_t partIndex, bool jumpToEndPage, size_t readLimitOverride,
                                                                 bool allowDeferredExpand)
{
    BR_LOAD_SCOPE("chapter-load");
    BR_PERF_LOG("chapter index=%ld limit=%u deferred=%u", static_cast<long>(partIndex), static_cast<unsigned>(readLimitOverride),
                static_cast<unsigned>(allowDeferredExpand));
#if !INKHUD_BOOKREADER_HAS_SD
    (void)partIndex;
    (void)jumpToEndPage;
    (void)readLimitOverride;
    (void)allowDeferredExpand;
    return false;
#else
    [[maybe_unused]] const uint32_t tStartMs = millis();
    if (!pagedBookMode || partIndex < 0 || partIndex >= static_cast<int32_t>(pagedParts.size())) {
        return false;
    }

    const BrowserEntry &part = pagedParts[static_cast<size_t>(partIndex)];
    std::string loadedText;
    bool partTruncated = false;
    bool needsPartExpansion = false;
    bool loadedFromPrefetch = false;
    if (prefetchedNextPart.partIndex == partIndex && prefetchedNextPart.path == part.path && !prefetchedNextPart.text.empty() &&
        !prefetchedNextPart.truncated) {
        loadedText = std::move(prefetchedNextPart.text);
        prefetchedNextPart.clear();
        loadedFromPrefetch = true;
        BR_PERF_LOG("loadPagedPart prefetch-hit index=%ld path=%s", static_cast<long>(partIndex), part.path.c_str());
    } else {
        const size_t fullReadLimit = maxReadBytesForFormat(part.format);
        const size_t readLimit = (readLimitOverride > 0U) ? std::min(fullReadLimit, readLimitOverride) : fullReadLimit;
        if (!loadPartText(part, readLimit, loadedText, partTruncated, &needsPartExpansion)) {
            status = "Can't load chapter: " + part.path;
            BR_PERF_LOG("loadPagedPart loadPartText-failed index=%ld path=%s totalMs=%lu", static_cast<long>(partIndex),
                        part.path.c_str(), static_cast<unsigned long>(millis() - tStartMs));
            return false;
        }
    }

    displayText = (partIndex == 0 && useEpubCoverForFirstPart) ? makeImageMarkerLine(epubCoverImagePath) : std::move(loadedText);
    openFilePath = part.path;
    openFileTruncated = partTruncated;
    wrappedLines.clear();
    wrappedLineStyles.clear();
    chapterOffsets.clear();
    chapterOffsets.push_back(0);
    for (const auto &entry : contents)
        if (entry.part == partIndex && entry.offset != std::string::npos)
            chapterOffsets.push_back(entry.offset);
    std::sort(chapterOffsets.begin(), chapterOffsets.end());
    chapterOffsets.erase(std::unique(chapterOffsets.begin(), chapterOffsets.end()), chapterOffsets.end());
    chapterStartLines.clear();
    chapterStartLines.push_back(0);
    wrapDirty = true;
    wrapWidth = 0;
    fileLoaded = true;
    pagedPartIndex = partIndex;
    chapterPickerSelected = getCurrentChapterIndex();

    const uint16_t headerBottom = getHeaderHeight();
    const uint16_t footerHeight = ReaderToolbar::progressFooterHeight(fontSmall.lineHeight());
    const uint16_t contentTop = headerBottom + 2;
    const uint16_t contentHeight = (height() > footerHeight && (height() - footerHeight) > contentTop)
                                       ? static_cast<uint16_t>(height() - footerHeight - contentTop - 1)
                                       : 0;
    [[maybe_unused]] const uint32_t tWrapStartMs = millis();
    const bool allowDeferredWrap = allowDeferredExpand && !jumpToEndPage;
    rewrapIfNeeded(width(), contentHeight, allowDeferredWrap);
    const int32_t step = std::max<int32_t>(1, linesPerPage);
    const int32_t maxStart = readerLastPageStart(wrappedLines.size(), step);
    scrollLine = jumpToEndPage ? maxStart : 0;
    bool imageOnlyPage = false;
    if (scrollLine == 0 && wrappedLines.size() == 1U) {
        size_t lineStart = 0;
        consumeLeadingLineXOffsetMarkers(wrappedLines.front().c_str(), lineStart);
        const std::string line = wrappedLines.front().c_str() + lineStart;
        imageOnlyPage = isImageMarkerLine(line, nullptr) || isImageSequenceMarkerLine(line, nullptr);
    }
    deferInlineImagesUntilNextRender = !imageOnlyPage && !allowDeferredWrap;
    status.clear();
    BR_PERF_LOG("loadPagedPart wrapped index=%ld lines=%u wrapMs=%lu", static_cast<long>(partIndex),
                static_cast<unsigned>(wrappedLines.size()), static_cast<unsigned long>(millis() - tWrapStartMs));

    deferredPartLoad.clear();
    if (allowDeferredExpand && needsPartExpansion && scrollLine == 0) {
        queueDeferredPartExpansion(partIndex);
    } else {
        updateBackgroundReaderPending();
    }

    (void)loadedFromPrefetch;
    queueDeferredNextPartPrefetch();
    BR_PERF_LOG("loadPagedPart done index=%ld prefetchHit=%u truncated=%u "
                "deferred=%u totalMs=%lu",
                static_cast<long>(partIndex), static_cast<unsigned>(loadedFromPrefetch), static_cast<unsigned>(partTruncated),
                static_cast<unsigned>(backgroundReaderWorkPending), static_cast<unsigned long>(millis() - tStartMs));
    return true;
#endif
}

BOOKREADER_NOINLINE bool InkHUD::BookReaderApplet::openSelectedEntry()
{
    BR_LOAD_SCOPE("book-open");
    if (selectedIndex < 0 || selectedIndex >= static_cast<int32_t>(entries.size())) {
        return false;
    }

    const BrowserEntry &entry = entries[static_cast<size_t>(selectedIndex)];
    BR_FLOW_LOG("openSelectedEntry index=%ld path=%s isDir=%u format=%u", static_cast<long>(selectedIndex), entry.path.c_str(),
                static_cast<unsigned>(entry.isDir), static_cast<unsigned>(entry.format));
    if (entry.isDir) {
        const bool ok = loadDirectory(entry.path);
        mode = Mode::BROWSER;
        BR_FLOW_LOG("openSelectedEntry directoryDone ok=%u path=%s", static_cast<unsigned>(ok), currentDir.c_str());
        return ok;
    }
    const bool ok = loadFile(entry);
    BR_FLOW_LOG("openSelectedEntry fileDone ok=%u path=%s mode=%u fileLoaded=%u", static_cast<unsigned>(ok), entry.path.c_str(),
                static_cast<unsigned>(mode), static_cast<unsigned>(fileLoaded));
    return ok;
}

void InkHUD::BookReaderApplet::beginOpenSelectedEntry()
{
    if (selectedIndex < 0 || selectedIndex >= static_cast<int32_t>(entries.size())) {
        return;
    }

    saveReadingPosition();
    noteBookReaderLoadActivity(true);
    pendingOpenIndex = selectedIndex;
    pendingChapterIndex = -1;
    pendingPageDelta = 0;
    openingSplashRendered = false;
    mode = Mode::OPENING;
    status = "Opening " + entries[static_cast<size_t>(selectedIndex)].name + "...";
    BR_FLOW_LOG("beginOpenSelectedEntry index=%ld path=%s", static_cast<long>(selectedIndex),
                entries[static_cast<size_t>(selectedIndex)].path.c_str());
    requestUpdate(Drivers::EInk::UpdateTypes::FAST, !useTouchFriendlyBookReaderLayout(inkhud));
}

void InkHUD::BookReaderApplet::goToParentDirectory()
{
    const std::string parent = parentDir(currentDir);
    if (parent == currentDir) {
        return;
    }
    loadDirectory(parent);
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

void InkHUD::BookReaderApplet::moveSelection(int delta)
{
    if (entries.empty()) {
        selectedIndex = -1;
        return;
    }
    if (selectedIndex < 0) {
        selectedIndex = 0;
        return;
    }

    selectedIndex += delta;
    if (selectedIndex < 0) {
        selectedIndex = 0;
    } else if (selectedIndex >= static_cast<int32_t>(entries.size())) {
        selectedIndex = static_cast<int32_t>(entries.size() - 1);
    }
}

void InkHUD::BookReaderApplet::ensureSelectionVisible(int32_t selected, int32_t &first, int32_t rowsVisible)
{
    if (rowsVisible <= 0)
        return;
    first = std::max<int32_t>(0, std::clamp(first, selected - rowsVisible + 1, selected));
}

void InkHUD::BookReaderApplet::wrapDisplayText(uint16_t contentWidth, size_t maxLines)
{
    setFont(readingFont());
    [[maybe_unused]] const uint32_t tWrapStartMs = millis();
    wrappedLines.clear();
    chapterStartLines.assign(chapterOffsets.size(), 0);
    wrappedLineStyles.clear();
    uint32_t imageMarkerCount = 0;
    const bool singleFullPageImage = isImageMarkerLine(displayText, nullptr) || isImageSequenceMarkerLine(displayText, nullptr);
    bool limitReached = false;
    deferredWrapExpansion = false;
    if (contentWidth == 0) {
        wrappedLines.push_back(" ");
        wrappedLineStyles.push_back(0);
        BR_PERF_LOG("wrapDisplayText width=0 lines=1 srcBytes=%u ms=%lu", static_cast<unsigned>(displayText.size()),
                    static_cast<unsigned long>(millis() - tWrapStartMs));
        return;
    }

    const uint16_t lineHeight = std::max<uint16_t>(1, readingFont().lineHeight());
    size_t nextChapterOffset = 0;
    auto markChapterStarts = [&](size_t sourcePos) {
        while (nextChapterOffset < chapterOffsets.size() && chapterOffsets[nextChapterOffset] <= sourcePos) {
            chapterStartLines[nextChapterOffset] = static_cast<int32_t>(wrappedLines.size());
            nextChapterOffset++;
        }
    };
    markChapterStarts(0);

    std::string line;
    std::string word;
    bool pendingSpace = false;
    uint8_t alignment = 1;

    auto trimRight = [](std::string &s) {
        while (!s.empty() && s.back() == ' ') {
            s.pop_back();
        }
    };

    struct GlyphWidth {
        uint8_t advance = 0;
        uint8_t width = 0;
        int8_t offset = 0;
        bool present = false;
    };
    std::array<GlyphWidth, 256> glyphWidths{};
    const GFXfont *font = readingFont().gfxFont;
    if (font) {
        const uint8_t first = font->first, last = font->last;
        for (uint16_t c = first; c <= last; ++c) {
            const auto &glyph = font->glyph[c - first];
            glyphWidths[c] = {glyph.xAdvance, glyph.width, glyph.xOffset, true};
        }
    }
    struct TextBounds {
        int32_t cursor = 0;
        int32_t left = INT32_MAX;
        int32_t right = -1;
        bool stopped = false;
    };
    auto extendBounds = [&](const std::string &s, TextBounds bounds) {
        // Match GFX text bounds, including bearings and zero-width spaces.
        for (size_t pos = 0; pos < s.size() && !bounds.stopped;) {
            size_t markerLength = 0;
            if (parseInlineStyleMarkerAt(s, pos, markerLength) != InlineStyleMarker::NONE) {
                pos += markerLength;
                continue;
            }
            const uint8_t c = static_cast<uint8_t>(s[pos++]);
            if (!c) {
                bounds.stopped = true;
                break;
            }
            if (c == '\n') {
                bounds.cursor = 0;
                continue;
            }
            if (c == '\r' || !glyphWidths[c].present)
                continue;
            const auto &glyph = glyphWidths[c];
            const int32_t start = bounds.cursor + glyph.offset;
            bounds.left = std::min(bounds.left, start);
            bounds.right = std::max(bounds.right, start + glyph.width - 1);
            bounds.cursor += glyph.advance;
        }
        return bounds;
    };
    auto boundsWidth = [&](const TextBounds &bounds) -> uint16_t {
        const int32_t left = std::min<int32_t>(contentWidth, bounds.left);
        return bounds.right >= left ? static_cast<uint16_t>(std::min<int32_t>(UINT16_MAX, bounds.right - left + 1)) : 0;
    };
    auto visibleWidth = [&](const std::string &s) -> uint16_t {
        return font ? boundsWidth(extendBounds(s, {})) : getTextWidth(stripInlineStyleMarkers(s));
    };

    auto nextTokenLen = [&](const std::string &s, size_t pos) -> size_t {
        size_t markerLen = 0;
        if (parseInlineStyleMarkerAt(s, pos, markerLen) != InlineStyleMarker::NONE && markerLen > 0) {
            return markerLen;
        }
        return 1;
    };

    std::vector<std::string> *cellLines = nullptr;
    auto pushLine = [&](const std::string &s, uint16_t xOffset) {
        if (limitReached) {
            return;
        }
        std::string wrapped = s.empty() ? " " : s;
        if (xOffset > 0) {
            wrapped = makeLineXOffsetMarker(xOffset) + wrapped;
        }
        if (alignment > 1)
            wrapped = std::string(alignment == 2 ? ALIGN_CENTER : ALIGN_RIGHT) + wrapped;
        if (cellLines) {
            cellLines->push_back(std::move(wrapped));
            return;
        }
        wrappedLines.emplace_back(wrapped.data(), wrapped.size());
        if (maxLines > 0U && wrappedLines.size() >= maxLines) {
            limitReached = true;
        }
    };

    auto wrapPlainLine = [&](const std::string &rawLine, uint16_t xOffset, uint16_t firstIndent) {
        const size_t bulletLength = std::strlen(NicheGraphics::InkHUD::BULLET_MARKER);
        const size_t bulletPosition = rawLine.find(NicheGraphics::InkHUD::BULLET_MARKER);
        bool bulletPending = bulletPosition != std::string::npos;
        std::string body = rawLine;
        if (bulletPending)
            body.erase(bulletPosition, bulletLength);
        if (bulletPending) {
            firstIndent = 0;
            alignment = 1;
            xOffset = std::min<uint16_t>(contentWidth - 1U, xOffset + std::max<uint16_t>(8, getTextWidth("M")));
        }
        uint16_t currentOffset = std::min<uint16_t>(contentWidth - 1U, xOffset + firstIndent);
        uint16_t availableWidth = std::max<uint16_t>(1, contentWidth > currentOffset ? contentWidth - currentOffset : 1);
        auto pushTextLine = [&](const std::string &text) {
            pushLine(bulletPending ? std::string(NicheGraphics::InkHUD::BULLET_MARKER) + text : text, currentOffset);
            bulletPending = false;
            currentOffset = xOffset;
            availableWidth = std::max<uint16_t>(1, contentWidth > xOffset ? contentWidth - xOffset : 1);
        };
        line.clear();
        TextBounds lineBounds;
        word.clear();
        pendingSpace = false;

        auto addWord = [&](const std::string &w) {
            if (w.empty() || limitReached) {
                return;
            }

            std::string remaining = w;
            while (!remaining.empty()) {
                const bool addSpace = pendingSpace && !line.empty();
                TextBounds candidateBounds = addSpace ? extendBounds(" ", lineBounds) : lineBounds;
                candidateBounds = extendBounds(remaining, candidateBounds);
                const uint16_t candidateWidth =
                    font ? boundsWidth(candidateBounds) : visibleWidth(line + (addSpace ? " " : "") + remaining);
                if (candidateWidth <= availableWidth) {
                    if (addSpace)
                        line.push_back(' ');
                    line += remaining;
                    lineBounds = candidateBounds;
                    pendingSpace = false;
                    remaining.clear();
                    break;
                }

                if (!line.empty()) {
                    trimRight(line);
                    pushTextLine(line);
                    if (limitReached) {
                        return;
                    }
                    line.clear();
                    lineBounds = {};
                    pendingSpace = false;
                    continue;
                }

                size_t split = 0;
                std::string piece;
                while (split < remaining.size()) {
                    const size_t tokLen = nextTokenLen(remaining, split);
                    if (tokLen == 0 || split + tokLen > remaining.size()) {
                        break;
                    }
                    const std::string test = piece + remaining.substr(split, tokLen);
                    if (!piece.empty() && visibleWidth(test) > availableWidth) {
                        break;
                    }
                    piece = test;
                    split += tokLen;

                    if (visibleWidth(piece) >= availableWidth) {
                        break;
                    }
                }

                if (piece.empty()) {
                    const size_t tokLen = std::min<size_t>(nextTokenLen(remaining, 0), remaining.size());
                    piece = remaining.substr(0, tokLen);
                    split = tokLen;
                }

                pushTextLine(piece);
                if (limitReached) {
                    return;
                }
                remaining.erase(0, split);
                pendingSpace = false;
            }
        };

        for (char c : body) {
            if (limitReached) {
                break;
            }
            if (c == '\r') {
                continue;
            }
            if (c == ' ' || c == '\t') {
                addWord(word);
                word.clear();
                pendingSpace = true;
                continue;
            }
            word.push_back(c);
        }

        addWord(word);
        word.clear();
        if (limitReached) {
            return;
        }
        trimRight(line);
        pushTextLine(line);
    };

    size_t lineStart = 0;
    uint8_t sourceStyle = 0;
    while (lineStart <= displayText.size()) {
        noteBookReaderLoadActivity();
        const size_t lineEnd = displayText.find('\n', lineStart);
        const bool hasNewline = lineEnd != std::string::npos;
        const size_t end = hasNewline ? lineEnd : displayText.size();
        const std::string rawLineWithMarkers = displayText.substr(lineStart, end - lineStart);
        size_t textStart = 0;
        alignment = 1;
        const uint16_t lineXOffset = consumeLeadingLineXOffsetMarkers(rawLineWithMarkers, textStart, &alignment);
        uint16_t firstIndent = 0;
        const size_t firstMarkerLength = std::strlen(NicheGraphics::InkHUD::FIRST_LINE_MARKER);
        if (rawLineWithMarkers.compare(textStart, firstMarkerLength, NicheGraphics::InkHUD::FIRST_LINE_MARKER) == 0) {
            size_t consumed = 0;
            firstIndent = consumeLeadingLineXOffsetMarkers(rawLineWithMarkers.substr(textStart + firstMarkerLength), consumed);
            textStart += firstMarkerLength + consumed;
        }
        const std::string rawLine = rawLineWithMarkers.substr(textStart);
        sourceStyle = inlineStyleAfter(rawLine, sourceStyle);

        if (stripInlineStyleMarkers(rawLine) == NicheGraphics::InkHUD::TABLE_START) {
            const size_t tableEnd = displayText.find(NicheGraphics::InkHUD::TABLE_END, end);
            if (tableEnd != std::string::npos) {
                std::vector<std::pair<size_t, std::vector<std::string>>> rows;
                size_t columns = 0;
                for (size_t pos = end + 1; pos < tableEnd;) {
                    const size_t newline = displayText.find('\n', pos);
                    const size_t rowEnd = std::min(tableEnd, newline == std::string::npos ? tableEnd : newline);
                    const std::string row = displayText.substr(pos, rowEnd - pos);
                    if (row.find(NicheGraphics::InkHUD::TABLE_ROW) != std::string::npos) {
                        sourceStyle = inlineStyleAfter(
                            std::string_view(row).substr(0, row.find(NicheGraphics::InkHUD::TABLE_CELL)), sourceStyle);
                        auto cells = NicheGraphics::InkHUD::readerTableCells(row);
                        for (auto &cell : cells) {
                            const uint8_t nextStyle = inlineStyleAfter(cell, sourceStyle);
                            cell = inlineStylePrefix(sourceStyle) + cell;
                            sourceStyle = nextStyle;
                        }
                        columns = std::max(columns, cells.size());
                        rows.emplace_back(rowEnd, std::move(cells));
                    } else if (isMeaningfulDisplayLine(row)) {
                        markChapterStarts(rowEnd);
                        alignment = 1;
                        wrapPlainLine(row, 0, 0);
                    }
                    if (row.find(NicheGraphics::InkHUD::TABLE_ROW) == std::string::npos)
                        sourceStyle = inlineStyleAfter(row, sourceStyle);
                    pos = rowEnd + 1;
                }
                if (columns && columns <= 16 && !limitReached) {
                    std::vector<uint32_t> preferred(columns, 1);
                    for (const auto &row : rows)
                        for (size_t col = 0; col < row.second.size(); ++col)
                            preferred[col] = std::max<uint32_t>(preferred[col], visibleWidth(row.second[col]) + 8U);
                    uint32_t totalPreferred = 0;
                    for (const auto value : preferred)
                        totalPreferred += value;
                    const uint16_t tableWidth = contentWidth;
                    const uint16_t minimumWidth = std::min<uint16_t>(28, tableWidth / columns);
                    const uint32_t extraWidth = tableWidth - minimumWidth * columns;
                    std::vector<uint16_t> widths(columns);
                    uint16_t usedWidth = 0;
                    for (size_t col = 0; col < columns; ++col) {
                        widths[col] = col + 1 == columns ? tableWidth - usedWidth
                                                         : minimumWidth + extraWidth * preferred[col] / totalPreferred;
                        usedWidth += widths[col];
                    }
                    alignment = 1;
                    for (const auto &row : rows) {
                        std::vector<std::vector<std::string>> wrappedCells(columns);
                        size_t rowHeight = 1;
                        for (size_t col = 0; col < columns; ++col) {
                            contentWidth = std::max<uint16_t>(1, widths[col] > 8 ? widths[col] - 8 : 1);
                            cellLines = &wrappedCells[col];
                            wrapPlainLine(col < row.second.size() ? row.second[col] : "", 0, 0);
                            uint8_t cellStyle = 0;
                            for (auto &cellLine : wrappedCells[col]) {
                                const uint8_t nextStyle = inlineStyleAfter(cellLine, cellStyle);
                                cellLine = inlineStylePrefix(cellStyle) + cellLine;
                                cellStyle = nextStyle;
                            }
                            rowHeight = std::max(rowHeight, wrappedCells[col].size());
                        }
                        cellLines = nullptr;
                        contentWidth = tableWidth;
                        const size_t pageLines = std::max<int32_t>(1, linesPerPage);
                        const size_t remaining = pageLines - wrappedLines.size() % pageLines;
                        if (rowHeight <= pageLines && rowHeight > remaining)
                            for (size_t pad = 0; pad < remaining && !limitReached; ++pad)
                                pushLine("", 0);
                        markChapterStarts(row.first);
                        for (size_t lineIndex = 0; lineIndex < rowHeight && !limitReached; ++lineIndex) {
                            const size_t pageLine = wrappedLines.size() % pageLines;
                            const unsigned borders = (lineIndex == 0 || pageLine == 0 ? 1U : 0U) |
                                                     (lineIndex + 1 == rowHeight || pageLine + 1 == pageLines ? 2U : 0U);
                            std::string rendered =
                                std::string(NicheGraphics::InkHUD::TABLE_LINE) + static_cast<char>('0' + borders);
                            for (size_t col = 0; col < columns; ++col)
                                rendered += std::string(NicheGraphics::InkHUD::TABLE_CELL) + std::to_string(widths[col]) + ":" +
                                            (lineIndex < wrappedCells[col].size() ? wrappedCells[col][lineIndex] : "");
                            pushLine(rendered, 0);
                        }
                        if (limitReached)
                            break;
                    }
                } else if (!limitReached) {
                    for (const auto &row : rows)
                        for (const auto &cell : row.second)
                            wrapPlainLine(cell, 0, 0);
                }
                if (limitReached)
                    break;
                lineStart = displayText.find('\n', tableEnd);
                sourceStyle = inlineStyleAfter(
                    std::string_view(displayText)
                        .substr(tableEnd, lineStart == std::string::npos ? std::string::npos : lineStart - tableEnd),
                    sourceStyle);
                if (columns > 16 && !wrappedLines.empty())
                    wrappedLines.back() +=
                        std::string(STYLE_BOLD_OFF) + STYLE_UNDERLINE_OFF + STYLE_STRIKE_OFF + inlineStylePrefix(sourceStyle);
                if (lineStart == std::string::npos)
                    break;
                ++lineStart;
                continue;
            }
        }

        std::string imagePath;
        std::vector<std::string> imagePaths;
        if (isImageMarkerLine(rawLine, &imagePath) || isImageSequenceMarkerLine(rawLine, &imagePaths)) {
            alignment = 1;
            imageMarkerCount++;
            const uint16_t imageWidth = (contentWidth > lineXOffset) ? (contentWidth - lineXOffset) : contentWidth;
            const std::string &spanImagePath = !imagePath.empty() ? imagePath : imagePaths.front();
            const int32_t pageLines = std::max<int32_t>(1, linesPerPage);
            const int32_t span =
                singleFullPageImage
                    ? 1
                    : std::clamp<int32_t>(estimateImageLineSpan(spanImagePath, imageWidth, lineHeight), 1, pageLines);
            const int32_t remaining = pageLines - static_cast<int32_t>(wrappedLines.size() % pageLines);
            if (span > remaining) {
                for (int32_t i = 0; i < remaining && !limitReached; ++i)
                    pushLine("", 0);
            }
            if (limitReached)
                break;
            markChapterStarts(end);
            pushLine(rawLine, lineXOffset);
            // A partial wrap must still reserve the whole image block.
            for (int32_t i = 1; i < span; i++) {
                wrappedLines.push_back(IMAGE_CONTINUATION_LINE);
                if (maxLines > 0U && wrappedLines.size() >= maxLines) {
                    limitReached = true;
                }
            }
        } else {
            // Inline section IDs belong to this paragraph, not the following one.
            markChapterStarts(end);
            wrapPlainLine(rawLine, lineXOffset, firstIndent);
        }

        if (limitReached) {
            break;
        }

        if (!hasNewline) {
            break;
        }
        lineStart = end + 1;
    }

    markChapterStarts(displayText.size());
    if (wrappedLines.empty()) {
        wrappedLines.push_back("(empty)");
    }
    while (nextChapterOffset < chapterStartLines.size()) {
        chapterStartLines[nextChapterOffset] = std::max<int32_t>(0, static_cast<int32_t>(wrappedLines.size()) - 1);
        nextChapterOffset++;
    }
    deferredWrapExpansion = limitReached;
    uint8_t style = 0;
    wrappedLineStyles.reserve(wrappedLines.size());
    for (const auto &wrapped : wrappedLines) {
        wrappedLineStyles.push_back(style);
        const std::string_view text(wrapped.data(), wrapped.size());
        if (text.find(NicheGraphics::InkHUD::TABLE_LINE) == std::string_view::npos)
            style = inlineStyleAfter(text, style);
    }
    BR_PERF_LOG("wrapDisplayText width=%u srcBytes=%u lines=%u images=%u "
                "chapters=%u ms=%lu",
                static_cast<unsigned>(contentWidth), static_cast<unsigned>(displayText.size()),
                static_cast<unsigned>(wrappedLines.size()), imageMarkerCount, static_cast<unsigned>(chapterOffsets.size()),
                static_cast<unsigned long>(millis() - tWrapStartMs));
}

void InkHUD::BookReaderApplet::rewrapIfNeeded(uint16_t contentWidth, uint16_t contentHeight, bool allowPartialWrap)
{
    const int32_t previousLinesPerPage = linesPerPage;
    if (contentHeight == 0) {
        linesPerPage = 1;
    } else {
        const uint16_t lh = std::max<uint16_t>(1, readingFont().lineHeight());
        linesPerPage = std::max<int32_t>(1, static_cast<int32_t>(contentHeight / lh));
    }

    if (wrapDirty || wrapWidth != contentWidth || previousLinesPerPage != linesPerPage) {
        const size_t initialLineLimit = allowPartialWrap ? static_cast<size_t>(std::max<int32_t>(linesPerPage * 2, 24)) : 0U;
        wrapDisplayText(contentWidth, initialLineLimit);
        wrapWidth = contentWidth;
        wrapDirty = false;
        if (deferredWrapExpansion) {
            queueDeferredWrapExpansion();
        }
    }

    const int32_t maxStart = readerLastPageStart(wrappedLines.size(), linesPerPage);
    if (scrollLine > maxStart) {
        scrollLine = maxStart;
    }
    if (scrollLine < 0) {
        scrollLine = 0;
    }
    scrollLine = (scrollLine / linesPerPage) * linesPerPage;
    if (pagedBookMode && !deferredWrapExpansion)
        updateContentsLines(pagedPartIndex);
}

uint16_t InkHUD::BookReaderApplet::alignedTextOffset(const std::string &line, uint16_t offset, uint8_t alignment)
{
    if (alignment > 1) {
        const uint16_t textWidth = getTextWidth(stripInlineStyleMarkers(line));
        const uint16_t available = width() > offset ? width() - offset : 0;
        if (textWidth < available)
            offset += (available - textWidth) / (alignment == 2 ? 2 : 1);
    }
    return offset;
}

bool InkHUD::BookReaderApplet::pageImagesNeedLoading(int32_t firstLine, int32_t count)
{
    setFont(readingFont());
    const uint16_t lh = std::max<uint16_t>(1, readingFont().lineHeight());
    const int32_t end = std::min<int32_t>(wrappedLines.size(), firstLine + count);
    for (int32_t index = firstLine; index < end; ++index) {
        const std::string line = wrappedLines[index].c_str();
        size_t start = 0;
        uint8_t alignment = 1;
        uint16_t offset = consumeLeadingLineXOffsetMarkers(line, start, &alignment);
        offset = alignedTextOffset(line.substr(start), offset, alignment);
        std::string path;
        std::vector<std::string> paths;
        const bool single = isImageMarkerLine(line.substr(start), &path);
        if (!single && !isImageSequenceMarkerLine(line.substr(start), &paths))
            continue;
        int32_t span = 1;
        while (index + span < static_cast<int32_t>(wrappedLines.size()) &&
               isImageContinuationLine(wrappedLines[index + span].c_str()))
            ++span;
        const uint16_t imageWidth = width() > offset ? width() - offset : width();
        const uint16_t imageHeight = std::max<uint16_t>(lh, static_cast<uint16_t>(span * lh));
        const std::string key = single ? imageCacheKey(path, imageWidth, imageHeight, IMAGE_ZOOM_NORMAL, true)
                                       : imageSequenceCacheKey(paths, imageWidth, imageHeight, IMAGE_ZOOM_NORMAL);
        if (!findRenderedImageCache(key))
            return true;
        index += span - 1;
    }
    return false;
}

void InkHUD::BookReaderApplet::scrollByPages(int deltaPages, bool showLoading)
{
    lastReaderInputMs = Time::skipZero(Time::getMillis());
    BR_PERF_LOG("page input delta=%d part=%ld line=%ld", deltaPages, static_cast<long>(pagedPartIndex),
                static_cast<long>(scrollLine));
    if (mode != Mode::READER || deltaPages == 0) {
        return;
    }
    closeExpandedImage();

    const int32_t step = std::max<int32_t>(1, linesPerPage);
    const int dir = (deltaPages > 0) ? 1 : -1;
    const int32_t targetPart = pagedPartIndex + dir;
    const bool needsPartExpansion =
        dir > 0 && deferredPartLoad.partIndex == pagedPartIndex && scrollLine >= readerLastPageStart(wrappedLines.size(), step);
    const bool crossesPart =
        dir > 0 ? !needsPartExpansion && !deferredWrapExpansion && scrollLine >= readerLastPageStart(wrappedLines.size(), step)
                : scrollLine == 0;
    const bool targetPagePrepared =
        (pageNavigation && dir > 0 && prefetchedNextPart.partIndex == targetPart && !deferredNextPartPrefetch) ||
        std::any_of(gRenderedImageCaches.begin(), gRenderedImageCaches.end(), [&](const auto &cache) {
            return cache.ready && cache.partIndex == targetPart && cache.viewportWidth == width() &&
                   cache.viewportHeight == height();
        });
    const bool needsPartLoad = pagedBookMode && crossesPart && targetPart >= 0 &&
                               targetPart < static_cast<int32_t>(pagedParts.size()) && !targetPagePrepared;
    const int32_t targetLine = std::clamp<int64_t>(static_cast<int64_t>(scrollLine) + static_cast<int64_t>(deltaPages) * step, 0,
                                                   readerLastPageStart(wrappedLines.size(), step));
    const bool needsImages = showLoading && !crossesPart && pageImagesNeedLoading(targetLine, step);
    if (showLoading && (needsPartLoad || needsImages || needsPartExpansion)) {
        pendingPageDelta = deltaPages;
        pendingOpenIndex = -1;
        pendingChapterIndex = -1;
        openingSplashRendered = false;
        mode = Mode::OPENING;
        status = "Loading...";
        requestUpdate(Drivers::EInk::UpdateTypes::FAST, false);
        return;
    }
    int32_t remaining = std::abs(deltaPages);
    while (remaining-- > 0) {
        int32_t maxStart = readerLastPageStart(wrappedLines.size(), step);
        if (dir > 0) {
            if (deferredPartLoad.partIndex == pagedPartIndex && scrollLine >= maxStart) {
                if (!runBackgroundReaderPass())
                    break;
                maxStart = readerLastPageStart(wrappedLines.size(), step);
            }
            if (deferredWrapExpansion && scrollLine >= maxStart) {
                wrapDisplayText(width(), static_cast<size_t>(scrollLine + step * 3));
                updateBackgroundReaderPending();
                maxStart = readerLastPageStart(wrappedLines.size(), step);
            }
            if (scrollLine < maxStart) {
                scrollLine = std::min(maxStart, scrollLine + step);
                continue;
            }
            if (pagedBookMode && loadPagedPart(pagedPartIndex + 1, false)) {
                continue;
            }
            break;
        }

        if (scrollLine > 0) {
            scrollLine = std::max<int32_t>(0, scrollLine - step);
            continue;
        }
        if (pagedBookMode && loadPagedPart(pagedPartIndex - 1, true)) {
            continue;
        }
        break;
    }
    if (!currentImagePageLine()) {
        const int32_t end = std::min<int32_t>(wrappedLines.size(), scrollLine + step);
        for (int32_t i = scrollLine; i < end; ++i) {
            const std::string line = wrappedLines[i].c_str();
            size_t start = 0;
            consumeLeadingLineXOffsetMarkers(line, start);
            std::string image;
            std::vector<std::string> images;
            if (isImageMarkerLine(line.substr(start), &image))
                images.push_back(image);
            else if (!isImageSequenceMarkerLine(line.substr(start), &images))
                continue;
            const bool cached = std::all_of(images.begin(), images.end(), [&](const auto &path) {
                return std::any_of(gRenderedImageCaches.begin(), gRenderedImageCaches.end(), [&](const auto &entry) {
                    return entry.ready && entry.path.find("|" + path) != std::string::npos;
                });
            });
            if (!cached) {
                deferInlineImagesUntilNextRender = true;
                break;
            }
        }
    }
}

void InkHUD::BookReaderApplet::resetImageView()
{
    imageInteractionPending = false;
    imagePanOnlyRender = false;
    imageViewportFilled = false;
    gImageDrawWidth = gImageDrawHeight = 0;
    imageViewKey.clear();
    imageZoomPermille = IMAGE_ZOOM_NORMAL;
    imagePanX = 0;
    imagePanY = 0;
}

void InkHUD::BookReaderApplet::closeExpandedImage()
{
    if (expandedImageLine.empty())
        return;
    expandedImageLine.clear();
    expandedImageLoading = false;
    resetImageView();
}

bool InkHUD::BookReaderApplet::currentImagePageLine(std::string *lineOut) const
{
    if (mode == Mode::READER && fileLoaded && !expandedImageLine.empty()) {
        if (lineOut)
            *lineOut = expandedImageLine;
        return true;
    }
    if (mode != Mode::READER || !fileLoaded || wrappedLines.size() != 1U || scrollLine != 0) {
        return false;
    }

    const std::string wrappedLine = wrappedLines.front().c_str();
    size_t lineStart = 0;
    consumeLeadingLineXOffsetMarkers(wrappedLine, lineStart);
    const std::string line = wrappedLine.substr(lineStart);
    if (!isImageMarkerLine(line, nullptr) && !isImageSequenceMarkerLine(line, nullptr)) {
        return false;
    }

    if (lineOut) {
        *lineOut = line;
    }
    return true;
}

bool InkHUD::BookReaderApplet::syncImageViewToCurrentPage()
{
    std::string line;
    if (!currentImagePageLine(&line)) {
        resetImageView();
        return false;
    }

    const std::string key = openFilePath + "#" + std::to_string(pagedPartIndex) + "#" + line;
    if (key != imageViewKey) {
        resetImageView();
        imageViewKey = key;
    }
    return true;
}

bool InkHUD::BookReaderApplet::cycleImageZoom(bool zoomOut)
{
    if (!syncImageViewToCurrentPage()) {
        return false;
    }

    size_t level = 0;
    for (size_t i = 0; i < IMAGE_ZOOM_LEVELS.size(); i++) {
        if (imageZoomPermille <= IMAGE_ZOOM_LEVELS[i]) {
            level = i;
            break;
        }
    }

    if (zoomOut) {
        level = (level > 0U) ? (level - 1U) : 0U;
    } else {
        level = std::min(level + 1U, IMAGE_ZOOM_LEVELS.size() - 1U);
    }

    const uint16_t previousZoom = imageZoomPermille;
    if (previousZoom == IMAGE_ZOOM_LEVELS[level])
        return false;
    imageZoomPermille = IMAGE_ZOOM_LEVELS[level];
    imagePanX = static_cast<int32_t>(imagePanX) * imageZoomPermille / previousZoom;
    imagePanY = static_cast<int32_t>(imagePanY) * imageZoomPermille / previousZoom;
    if (imageZoomPermille == IMAGE_ZOOM_NORMAL) {
        imagePanX = 0;
        imagePanY = 0;
    }
    return true;
}

bool InkHUD::BookReaderApplet::panImageView(int16_t dx, int16_t dy)
{
    if (!syncImageViewToCurrentPage() || imageZoomPermille <= IMAGE_ZOOM_NORMAL) {
        return false;
    }

    const int32_t drawWidth =
        std::min<uint32_t>(4096U, static_cast<uint32_t>(gImageDrawWidth) * imageZoomPermille / gImageDrawZoomPermille);
    const int32_t drawHeight =
        std::min<uint32_t>(4096U, static_cast<uint32_t>(gImageDrawHeight) * imageZoomPermille / gImageDrawZoomPermille);
    const int32_t limitX = std::max<int32_t>(0, (drawWidth - gImageViewportWidth) / 2);
    const int32_t limitY = std::max<int32_t>(0, (drawHeight - gImageViewportHeight) / 2);
    const int16_t nextX = std::clamp<int32_t>(static_cast<int32_t>(imagePanX) + dx, -limitX, limitX);
    const int16_t nextY = std::clamp<int32_t>(static_cast<int32_t>(imagePanY) + dy, -limitY, limitY);
    const bool changed = nextX != imagePanX || nextY != imagePanY;
    imagePanX = nextX;
    imagePanY = nextY;
    return changed;
}

void InkHUD::BookReaderApplet::queueImageInteractionUpdate(bool panOnly)
{
    // Combine rapid gestures without drawing an intermediate preview or lowering grayscale quality.
    // Retain chrome only when the image overwrites every pixel in its unchanged viewport.
    const bool retainChrome = panOnly && imageViewportFilled && !backgroundReaderWorkPending &&
                              !deferInlineImagesUntilNextRender && !expandedImageLoading &&
                              !(wantsToRender() && wantsFullRender());
    imagePanOnlyRender = imageInteractionPending ? imagePanOnlyRender && retainChrome : retainChrome;
    imageInteractionPending = true;
    lastReaderInputMs = Time::skipZero(Time::getMillis());
    concurrency::OSThread::enabled = true;
    setIntervalFromNow(150);
}

int32_t InkHUD::BookReaderApplet::estimateImageLineSpan(const std::string &imagePath, uint16_t contentWidth,
                                                        uint16_t lineHeight) const
{
    if (lineHeight == 0U) {
        return IMAGE_MIN_RESERVED_LINES;
    }

    uint32_t desiredHeight = IMAGE_DEFAULT_HEIGHT_PX;
    const ImageProbeResult probe = probeImageFile(imagePath);
    if (probe.valid && probe.width > 0U && probe.height > 0U && contentWidth > 0U) {
        desiredHeight = (static_cast<uint32_t>(contentWidth) * static_cast<uint32_t>(probe.height)) / probe.width;
    }
    desiredHeight = std::clamp<uint32_t>(desiredHeight, IMAGE_MIN_HEIGHT_PX, IMAGE_MAX_HEIGHT_PX);

    const uint32_t total = desiredHeight + IMAGE_VERTICAL_PADDING_PX;
    int32_t lines = static_cast<int32_t>((total + lineHeight - 1U) / lineHeight);
    lines = std::clamp<int32_t>(lines, IMAGE_MIN_RESERVED_LINES, IMAGE_MAX_RESERVED_LINES);
    return lines;
}

bool InkHUD::BookReaderApplet::drawInlineImage(const std::string &imagePath, int16_t left, int16_t top, uint16_t maxWidth,
                                               uint16_t maxHeight, uint16_t zoomPermille, int16_t panX, int16_t panY,
                                               std::vector<uint8_t> *monoBitmap, bool useRenderedCache, bool preloadOnly,
                                               bool inlineRaster, int16_t clipBottom, bool *prefetchInterrupted,
                                               uint16_t *bitmapWidth, uint16_t *bitmapHeight)
{
    if (bitmapWidth)
        *bitmapWidth = 0;
    if (bitmapHeight)
        *bitmapHeight = 0;
    if (prefetchInterrupted)
        *prefetchInterrupted = false;
#if !INKHUD_BOOKREADER_HAS_SD || !INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES
    (void)imagePath;
    (void)left;
    (void)top;
    (void)maxWidth;
    (void)maxHeight;
    (void)zoomPermille;
    (void)panX;
    (void)panY;
    (void)monoBitmap;
    (void)useRenderedCache;
    (void)preloadOnly;
    (void)inlineRaster;
    (void)clipBottom;
    return false;
#else
    [[maybe_unused]] const uint32_t tImageStartMs = millis();
    BR_LOAD_SCOPE("image-load");
    [[maybe_unused]] uint32_t probeMs = 0;
    [[maybe_unused]] uint32_t decodeMs = 0;
    if (imagePath.empty() || maxWidth == 0U || maxHeight == 0U) {
        return false;
    }
    noteBookReaderLoadActivity(true);
    const bool imageInEpub = isEpubVirtualPath(imagePath);
    BR_PERF_LOG("image request path=%s target=%ux%u zoom=%u prefetch=%u epub=%u", imagePath.c_str(), maxWidth, maxHeight,
                zoomPermille, preloadOnly, imageInEpub);
    if (!hasRenderableImageExtension(imagePath)) {
        return false;
    }
    const uint16_t requestedZoom = zoomPermille;
    const std::string cacheKey = imageCacheKey(imagePath, maxWidth, maxHeight, zoomPermille, inlineRaster);
    if (!preloadOnly && useRenderedCache && !monoBitmap &&
        drawRenderedImageCache(this, cacheKey, left, top, maxWidth, maxHeight, zoomPermille, panX, panY, clipBottom)) {
        BR_PERF_LOG("image rendered-cache HIT path=%s", imagePath.c_str());
        noteBookReaderLoadActivity(true);
        return true;
    }
    if (!ensureSDReady()) {
        return false;
    }
    [[maybe_unused]] const uint32_t tProbeStartMs = millis();
    const ImageProbeResult probe = probeImageFile(imagePath);
    probeMs = millis() - tProbeStartMs;
    if (!probe.valid || probe.width == 0U || probe.height == 0U) {
        BR_PERF_LOG("image probe failed: %s", imagePath.c_str());
        BR_PERF_LOG("drawInlineImage invalid-probe path=%s probeMs=%lu totalMs=%lu", imagePath.c_str(),
                    static_cast<unsigned long>(probeMs), static_cast<unsigned long>(millis() - tImageStartMs));
        return false;
    }

    uint16_t drawWidth = maxWidth;
    uint16_t drawHeight = maxHeight;
    {
        uint32_t candidateW = maxWidth;
        uint32_t candidateH = (candidateW * static_cast<uint32_t>(probe.height)) / probe.width;
        if (candidateH > maxHeight) {
            candidateH = maxHeight;
            candidateW = (candidateH * static_cast<uint32_t>(probe.width)) / probe.height;
        }
        const uint32_t zoom = std::max<uint16_t>(IMAGE_ZOOM_NORMAL, zoomPermille);
        candidateW = std::max<uint32_t>(1U, (candidateW * zoom) / IMAGE_ZOOM_NORMAL);
        candidateH = std::max<uint32_t>(1U, (candidateH * zoom) / IMAGE_ZOOM_NORMAL);
        drawWidth = std::max<uint16_t>(1, static_cast<uint16_t>(std::min<uint32_t>(candidateW, 4096U)));
        drawHeight = std::max<uint16_t>(1, static_cast<uint16_t>(std::min<uint32_t>(candidateH, 4096U)));
    }
    if (bitmapWidth)
        *bitmapWidth = drawWidth;
    if (bitmapHeight)
        *bitmapHeight = drawHeight;
    std::vector<uint8_t> localMonoBitmap;
    std::vector<uint8_t> preloadBitmap;
    if (preloadOnly)
        monoBitmap = &preloadBitmap;
    const bool captureCacheBitmap = (useRenderedCache && !monoBitmap);
    if (captureCacheBitmap) {
        if (!allocateImageBitmap(localMonoBitmap, monoBitmapBytes(drawWidth, drawHeight)))
            return false;
        monoBitmap = &localMonoBitmap;
    } else if (monoBitmap && !preloadOnly) {
        if (!allocateImageBitmap(*monoBitmap, monoBitmapBytes(drawWidth, drawHeight)))
            return false;
    }

    const int32_t maxPanX = (drawWidth > maxWidth) ? static_cast<int32_t>((drawWidth - maxWidth) / 2U) : 0;
    const int32_t maxPanY = (drawHeight > maxHeight) ? static_cast<int32_t>((drawHeight - maxHeight) / 2U) : 0;
    const int32_t clampedPanX = std::clamp<int32_t>(panX, -maxPanX, maxPanX);
    const int32_t clampedPanY = std::clamp<int32_t>(panY, -maxPanY, maxPanY);
    const int16_t imageLeft =
        static_cast<int16_t>(static_cast<int32_t>(left) + ((static_cast<int32_t>(maxWidth) - drawWidth) / 2) - clampedPanX);
    const int16_t imageTop =
        static_cast<int16_t>(static_cast<int32_t>(top) + ((static_cast<int32_t>(maxHeight) - drawHeight) / 2) - clampedPanY);

    // Bitmap-only rendering must not clear the screen while composing an image sequence.
    if (!monoBitmap && !preloadOnly) {
        fillRect(left, top, maxWidth, maxHeight, WHITE);
    }

    constexpr size_t maxSourceBytes = SOURCE_IMAGE_MAX_BYTES;
    const bool cacheSource = monoBitmap && (!inlineRaster || INKHUD_BOOKREADER_GRAYSCALE);
    const bool nativeSource = cacheSource && monoBitmapBytes(probe.width, probe.height) <= maxSourceBytes;
    const uint8_t sourceScale =
        !nativeSource && !preloadOnly && probe.type == ImageFileType::JPEG
            ? pickJpegScalePower(probe.width, probe.height, drawWidth, drawHeight, INKHUD_BOOKREADER_JPEG_MIN_TARGET_DIVISOR)
            : 0;
    const uint16_t sourceWidth = scaledDimensionForPowerOfTwo(probe.width, sourceScale);
    const uint16_t sourceHeight = scaledDimensionForPowerOfTwo(probe.height, sourceScale);
#if INKHUD_BOOKREADER_USE_JPEGDEC
    if (!preloadOnly && nativeSource && !sourceScale)
        collectJpegPrefetch(imagePath, true, displayText);
#endif
    auto *sourceEntry = findDecodedImageSlot(imagePath, preloadOnly ? &displayText : nullptr);
    if (!sourceEntry)
        return false;
#if INKHUD_BOOKREADER_GRAYSCALE
    if (preloadOnly && !sourceImagePrefetchFits(monoBitmapBytes(sourceWidth, sourceHeight), sourceEntry, displayText))
        return false;
#endif
    RenderedImageCache &gDecodedImage = *sourceEntry;
    gDecodedImage.lastUsed = ++gDecodedImageClock;
    const bool reuseSource = cacheSource && gDecodedImage.ready && gDecodedImage.path == imagePath &&
                             gDecodedImage.width >= sourceWidth && gDecodedImage.height >= sourceHeight;
    std::vector<uint8_t> *sourceBitmap = nullptr;
    if (cacheSource && !reuseSource && monoBitmapBytes(sourceWidth, sourceHeight) <= maxSourceBytes) {
        gDecodedImage.ready = false;
        gDecodedImage.path = imagePath;
        gDecodedImage.width = sourceWidth;
        gDecodedImage.height = sourceHeight;
#if INKHUD_BOOKREADER_GRAYSCALE
        trimSourceImages(monoBitmapBytes(sourceWidth, sourceHeight), &gDecodedImage, preloadOnly ? &displayText : nullptr);
#endif
        if (allocateImageBitmap(gDecodedImage.bitmap, monoBitmapBytes(sourceWidth, sourceHeight), &gDecodedImage,
                                preloadOnly ? &displayText : nullptr))
            sourceBitmap = &gDecodedImage.bitmap;
    }
    bool rendered = reuseSource;
    BR_PERF_LOG("image source path=%s cacheHit=%u inline=%u scale=%u probeMs=%lu", imagePath.c_str(),
                static_cast<unsigned>(reuseSource), static_cast<unsigned>(inlineRaster), static_cast<unsigned>(sourceScale),
                static_cast<unsigned long>(probeMs));
    if (!reuseSource && probe.type == ImageFileType::JPEG) {
        [[maybe_unused]] const uint32_t tDecodeStartMs = millis();
        std::vector<uint8_t> imageBytes;
        const std::vector<uint8_t> *decodeBytes = nullptr;
#if INKHUD_BOOKREADER_USE_JPEGDEC
        EpubJpegStream epubStream;
        const bool streamEpubImage = imageInEpub && prepareEpubJpegStream(imagePath, epubStream);
        epubStream.readAheadEnabled = streamEpubImage && !preloadOnly && portNUM_PROCESSORS > 1 && epubStream.dataSize >= 131072U;
#else
        const bool streamEpubImage = false;
#endif
        bool truncated = false;
        if (imageInEpub && !streamEpubImage) {
            if (!readEpubVirtualBinaryFileLimited(imagePath, MAX_EPUB_INLINE_IMAGE_BYTES, imageBytes, &truncated) ||
                imageBytes.empty() || truncated) {
                BR_PERF_LOG("drawInlineImage jpeg-read-failed path=%s truncated=%u "
                            "probeMs=%lu totalMs=%lu",
                            imagePath.c_str(), static_cast<unsigned>(truncated), static_cast<unsigned long>(probeMs),
                            static_cast<unsigned long>(millis() - tImageStartMs));
                return false;
            }
            decodeBytes = &imageBytes;
        }

#if INKHUD_BOOKREADER_USE_JPEGDEC
        JpegDecRenderContext ctx;
        BR_PERF_LOG("image input ready path=%s stream=%u bytes=%u inputMs=%lu", imagePath.c_str(),
                    static_cast<unsigned>(streamEpubImage), static_cast<unsigned>(imageBytes.size()),
                    static_cast<unsigned long>(millis() - tDecodeStartMs));
        ctx.applet = this;
        ctx.left = imageLeft;
        ctx.top = imageTop;
        ctx.outWidth = drawWidth;
        ctx.outHeight = drawHeight;
        ctx.srcWidth = probe.width;
        ctx.srcHeight = probe.height;
        ctx.monoBitmap = monoBitmap;
        ctx.sourceBitmap = sourceBitmap;
        ctx.skipScreenWrite = monoBitmap != nullptr;
        ctx.inlineRaster = inlineRaster;
        ctx.prefetchInterrupted = preloadOnly ? prefetchInterrupted : nullptr;

        const int openRc =
            streamEpubImage
                ? gBookReaderJpegDec.open(&epubStream, static_cast<int>(epubStream.dataSize), epubJpegCloseCallback,
                                          epubJpegReadCallback, epubJpegSeekCallback, jpegDecDrawCallback)
                : (decodeBytes ? gBookReaderJpegDec.openRAM(const_cast<uint8_t *>(decodeBytes->data()),
                                                            static_cast<int>(decodeBytes->size()), jpegDecDrawCallback)
                               : JPEG_INVALID_PARAMETER);
        int decodeRc = JPEG_INVALID_PARAMETER;
        int options = 0;
        const uint8_t scalePower = sourceScale;
        if (scalePower == 1U) {
            options |= JPEG_SCALE_HALF;
            ctx.srcWidth = scaledDimensionForPowerOfTwo(probe.width, scalePower);
            ctx.srcHeight = scaledDimensionForPowerOfTwo(probe.height, scalePower);
        } else if (scalePower == 2U) {
            options |= JPEG_SCALE_QUARTER;
            ctx.srcWidth = scaledDimensionForPowerOfTwo(probe.width, scalePower);
            ctx.srcHeight = scaledDimensionForPowerOfTwo(probe.height, scalePower);
        } else if (scalePower >= 3U) {
            options |= JPEG_SCALE_EIGHTH;
            ctx.srcWidth = scaledDimensionForPowerOfTwo(probe.width, 3U);
            ctx.srcHeight = scaledDimensionForPowerOfTwo(probe.height, 3U);
        }
        if ((streamEpubImage || decodeBytes) && openRc != 0) {
            BR_PERF_LOG("JPEG opened path=%s size=%dx%d stream=%u sourceBitmap=%u scale=%u", imagePath.c_str(),
                        gBookReaderJpegDec.getWidth(), gBookReaderJpegDec.getHeight(), streamEpubImage, sourceBitmap != nullptr,
                        sourceScale);
            gBookReaderJpegDec.setPixelType(sourceBitmap ? EIGHT_BIT_GRAYSCALE : RGB565_LITTLE_ENDIAN);
            gBookReaderJpegDec.setUserPointer(&ctx);
            {
                BR_LOAD_SCOPE("jpeg-codec-and-raster");
                decodeRc = gBookReaderJpegDec.decode(0, 0, options);
            }
            rendered = (decodeRc != 0);
            BR_PERF_LOG("JPEG decoded path=%s result=%d error=%d", imagePath.c_str(), decodeRc,
                        gBookReaderJpegDec.getLastError());
            epubStream.finishReadAhead();
#if INKHUD_BOOKREADER_PERF_LOG
            if (streamEpubImage)
                BR_PERF_LOG("JPEG stream IO path=%s reads=%lu readMs=%lu aheadHits=%lu waitMs=%lu", imagePath.c_str(),
                            static_cast<unsigned long>(epubStream.readCalls), static_cast<unsigned long>(epubStream.readMs),
                            static_cast<unsigned long>(epubStream.readAheadHits),
                            static_cast<unsigned long>(epubStream.readAheadWaitMs));
#endif
            gBookReaderJpegDec.close();
        }
        epubStream.finishReadAhead();
        if (prefetchInterrupted && *prefetchInterrupted) {
            BR_PERF_LOG("image prefetch interrupted path=%s", imagePath.c_str());
            return false;
        }
        if (!rendered && streamEpubImage) {
            BR_PERF_LOG("streamed JPEG failed: %s open=%d decode=%d error=%d; "
                        "retrying from RAM",
                        imagePath.c_str(), openRc, decodeRc, gBookReaderJpegDec.getLastError());
            if (readEpubVirtualBinaryFileLimited(imagePath, MAX_EPUB_INLINE_IMAGE_BYTES, imageBytes, &truncated) &&
                !imageBytes.empty() && !truncated) {
                if (sourceBitmap)
                    std::fill(sourceBitmap->begin(), sourceBitmap->end(), 0U);
                else if (monoBitmap)
                    std::fill(monoBitmap->begin(), monoBitmap->end(), 0U);
                if (gBookReaderJpegDec.openRAM(imageBytes.data(), static_cast<int>(imageBytes.size()), jpegDecDrawCallback)) {
                    gBookReaderJpegDec.setPixelType(sourceBitmap ? EIGHT_BIT_GRAYSCALE : RGB565_LITTLE_ENDIAN);
                    gBookReaderJpegDec.setUserPointer(&ctx);
                    rendered = gBookReaderJpegDec.decode(0, 0, options) != 0;
                    BR_PERF_LOG("RAM retry: path=%s bytes=%u rendered=%u error=%d", imagePath.c_str(),
                                static_cast<unsigned>(imageBytes.size()), rendered, gBookReaderJpegDec.getLastError());
                    gBookReaderJpegDec.close();
                } else {
                    BR_PERF_LOG("RAM JPEG open failed: %s bytes=%u error=%d", imagePath.c_str(),
                                static_cast<unsigned>(imageBytes.size()), gBookReaderJpegDec.getLastError());
                }
            } else {
                BR_PERF_LOG("RAM retry read failed: %s bytes=%u truncated=%u", imagePath.c_str(),
                            static_cast<unsigned>(imageBytes.size()), truncated);
            }
        }
        if (!rendered && !(prefetchInterrupted && *prefetchInterrupted)) {
            if (sourceBitmap)
                std::fill(sourceBitmap->begin(), sourceBitmap->end(), 0U);
            else if (monoBitmap)
                std::fill(monoBitmap->begin(), monoBitmap->end(), 0U);
            else
                fillRect(left, top, maxWidth, maxHeight, WHITE);
            rendered = decodeRomJpeg(decodeBytes ? *decodeBytes : imageBytes, ctx, sourceScale);
        }
        decodeMs = millis() - tDecodeStartMs;
        if (!rendered) {
            BR_PERF_LOG(
                "drawInlineImage jpegdec-failed path=%s open=%d decode=%d last=%d "
                "size=%lu probeMs=%lu decodeMs=%lu totalMs=%lu",
                imagePath.c_str(), openRc, decodeRc, gBookReaderJpegDec.getLastError(),
                static_cast<unsigned long>(streamEpubImage ? epubStream.dataSize : (decodeBytes ? decodeBytes->size() : 0U)),
                static_cast<unsigned long>(probeMs), static_cast<unsigned long>(decodeMs),
                static_cast<unsigned long>(millis() - tImageStartMs));
        }
#else
        ImageRenderContext ctx;
        const uint8_t scalePower = sourceScale;
        ctx.applet = this;
        ctx.left = imageLeft;
        ctx.top = imageTop;
        ctx.outWidth = drawWidth;
        ctx.outHeight = drawHeight;
        ctx.srcWidth = scaledDimensionForPowerOfTwo(probe.width, scalePower);
        ctx.srcHeight = scaledDimensionForPowerOfTwo(probe.height, scalePower);
        ctx.monoBitmap = monoBitmap;
        ctx.sourceBitmap = sourceBitmap;
        ctx.skipScreenWrite = monoBitmap != nullptr;
        ctx.inlineRaster = inlineRaster;
        ctx.prefetchInterrupted = preloadOnly ? prefetchInterrupted : nullptr;
        rendered = decodeBytes && decodeRomJpeg(*decodeBytes, ctx, scalePower);
        decodeMs = millis() - tDecodeStartMs;
        if (!rendered) {
            BR_PERF_LOG("drawInlineImage jpeg-decode-failed path=%s size=%lu "
                        "probeMs=%lu decodeMs=%lu totalMs=%lu",
                        imagePath.c_str(), static_cast<unsigned long>(decodeBytes ? decodeBytes->size() : 0U),
                        static_cast<unsigned long>(probeMs), static_cast<unsigned long>(decodeMs),
                        static_cast<unsigned long>(millis() - tImageStartMs));
        }
#endif
    } else if (!reuseSource && probe.type == ImageFileType::PNG) {
        [[maybe_unused]] const uint32_t tDecodeStartMs = millis();
        // PNG decoder is also large; using a static instance prevents stack
        // overflow.
        static PNG png;
        PngRenderContext ctx;
        ctx.applet = this;
        ctx.decoder = &png;
        ctx.left = imageLeft;
        ctx.top = imageTop;
        ctx.outWidth = drawWidth;
        ctx.outHeight = drawHeight;
        ctx.srcWidth = probe.width;
        ctx.srcHeight = probe.height;
        ctx.monoBitmap = monoBitmap;
        ctx.sourceBitmap = sourceBitmap;
        ctx.skipScreenWrite = monoBitmap != nullptr;
        ctx.inlineRaster = inlineRaster;
        ctx.prefetchInterrupted = preloadOnly ? prefetchInterrupted : nullptr;

        std::vector<uint8_t> imageBytes;
        bool truncated = false;
        if (!readEpubVirtualBinaryFileLimited(imagePath, MAX_EPUB_INLINE_IMAGE_BYTES, imageBytes, &truncated) ||
            imageBytes.empty() || truncated || imageBytes.size() > 0x7FFFFFFFU)
            return false;
        const int rcOpen = png.openRAM(imageBytes.data(), static_cast<int>(imageBytes.size()), pngDrawCallback);
        if (rcOpen == PNG_SUCCESS) {
            ctx.srcWidth = std::max<uint16_t>(1, static_cast<uint16_t>(png.getWidth()));
            ctx.srcHeight = std::max<uint16_t>(1, static_cast<uint16_t>(png.getHeight()));
            const int rcDecode = png.decode(&ctx, PNG_FAST_PALETTE);
            png.close();
            rendered = (rcDecode == PNG_SUCCESS);
        }
        decodeMs = millis() - tDecodeStartMs;
    }

    if (prefetchInterrupted && *prefetchInterrupted) {
        BR_PERF_LOG("image prefetch interrupted path=%s", imagePath.c_str());
        return false;
    }
    if (rendered && (reuseSource || sourceBitmap)) {
        gDecodedImage.ready = true;
        if (!preloadOnly) {
            BR_LOAD_SCOPE("image-source-scale");
            rendered = scaleSourceBitmap(gDecodedImage.bitmap, gDecodedImage.width, gDecodedImage.height, drawWidth, drawHeight,
                                         *monoBitmap);
        }
    }
    if (!rendered) {
        BR_PERF_LOG("image decode failed: %s", imagePath.c_str());
        if (!preloadOnly)
            fillRect(left, top, maxWidth, maxHeight, WHITE);
    }
    [[maybe_unused]] const char *typeName =
        (probe.type == ImageFileType::JPEG) ? "jpeg" : ((probe.type == ImageFileType::PNG) ? "png" : "unknown");
    BR_PERF_LOG("drawInlineImage path=%s type=%s src=%ux%u dst=%ux%u rendered=%u "
                "probeMs=%lu decodeMs=%lu totalMs=%lu",
                imagePath.c_str(), typeName, static_cast<unsigned>(probe.width), static_cast<unsigned>(probe.height),
                static_cast<unsigned>(drawWidth), static_cast<unsigned>(drawHeight), static_cast<unsigned>(rendered),
                static_cast<unsigned long>(probeMs), static_cast<unsigned long>(decodeMs),
                static_cast<unsigned long>(millis() - tImageStartMs));
    if (rendered && captureCacheBitmap && !localMonoBitmap.empty()) {
        storeRenderedImageCache(cacheKey, drawWidth, drawHeight, std::move(localMonoBitmap));
        [[maybe_unused]] RenderedImageCache *cache = findRenderedImageCache(cacheKey);
        if (cache && requestedZoom == IMAGE_ZOOM_NORMAL && wrappedLines.size() == 1U && scrollLine == 0) {
            cache->partIndex = pagedPartIndex;
            cache->viewportWidth = width();
            cache->viewportHeight = height();
        }
        BR_PERF_LOG("drawInlineImage cached key=%s size=%ux%u bytes=%u", cacheKey.c_str(), static_cast<unsigned>(drawWidth),
                    static_cast<unsigned>(drawHeight), static_cast<unsigned>(cache ? cache->bitmap.size() : 0U));
        drawRenderedImageCache(this, cacheKey, left, top, maxWidth, maxHeight, requestedZoom, panX, panY, clipBottom);
    }
    noteBookReaderLoadActivity(true);
    return rendered;
#endif
}

bool InkHUD::BookReaderApplet::drawInlineImageSequence(const std::vector<std::string> &imagePaths, int16_t left, int16_t top,
                                                       uint16_t maxWidth, uint16_t maxHeight, uint16_t zoomPermille, int16_t panX,
                                                       int16_t panY, bool useRenderedCache)
{
#if !INKHUD_BOOKREADER_HAS_SD || !INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES
    (void)imagePaths;
    (void)left;
    (void)top;
    (void)maxWidth;
    (void)maxHeight;
    (void)zoomPermille;
    (void)panX;
    (void)panY;
    (void)useRenderedCache;
    return false;
#else
    if (imagePaths.empty() || maxWidth == 0U || maxHeight == 0U) {
        return false;
    }
#if INKHUD_BOOKREADER_GRAYSCALE
    useRenderedCache = true;
#endif
    noteBookReaderLoadActivity(true);
    const uint16_t requestedZoom = zoomPermille;
    const std::string cacheKey = imageSequenceCacheKey(imagePaths, maxWidth, maxHeight, zoomPermille);
    if (useRenderedCache && drawRenderedImageCache(this, cacheKey, left, top, maxWidth, maxHeight, zoomPermille, panX, panY)) {
        noteBookReaderLoadActivity(true);
        return true;
    }
    std::vector<ImageProbeResult> probes;
    probes.reserve(imagePaths.size());
    uint32_t combinedHeight = 0U;
    uint16_t maxSourceWidth = 0U;
    for (const std::string &imagePath : imagePaths) {
        if (imagePath.empty()) {
            return false;
        }
        const ImageProbeResult probe = probeImageFile(imagePath);
        if (!probe.valid || probe.width == 0U || probe.height == 0U) {
            BR_PERF_LOG("drawInlineImageSequence invalid-probe path=%s", imagePath.c_str());
            return false;
        }
        probes.push_back(probe);
        combinedHeight += probe.height;
        maxSourceWidth = std::max<uint16_t>(maxSourceWidth, probe.width);
    }

    if (combinedHeight == 0U || maxSourceWidth == 0U) {
        return false;
    }

    uint32_t scaleNum = maxWidth;
    uint32_t scaleDen = maxSourceWidth;
    const uint32_t widthLimitedHeight =
        static_cast<uint32_t>((static_cast<uint64_t>(combinedHeight) * static_cast<uint64_t>(maxWidth)) / maxSourceWidth);
    if (widthLimitedHeight > maxHeight) {
        scaleNum = maxHeight;
        scaleDen = combinedHeight;
    }
    if (scaleNum == 0U || scaleDen == 0U) {
        return false;
    }
    scaleNum *= std::max<uint16_t>(IMAGE_ZOOM_NORMAL, zoomPermille);
    scaleDen *= IMAGE_ZOOM_NORMAL;

    std::vector<uint16_t> targetHeights;
    targetHeights.reserve(probes.size());
    uint32_t totalDrawHeight = 0U;
    uint16_t maxTargetWidth = 0U;
    for (const ImageProbeResult &probe : probes) {
        const uint16_t targetHeight =
            std::max<uint16_t>(1U, static_cast<uint16_t>((static_cast<uint64_t>(probe.height) * scaleNum) / scaleDen));
        targetHeights.push_back(targetHeight);
        totalDrawHeight += targetHeight;
        const uint16_t targetWidth =
            std::max<uint16_t>(1U, static_cast<uint16_t>((static_cast<uint64_t>(probe.width) * scaleNum) / scaleDen));
        maxTargetWidth = std::max<uint16_t>(maxTargetWidth, targetWidth);
    }

    const bool captureCacheBitmap = useRenderedCache;
    if (!captureCacheBitmap) {
        fillRect(left, top, maxWidth, maxHeight, WHITE);
    }
    std::vector<uint8_t> compositeBitmap;
    if (captureCacheBitmap) {
        if (!allocateImageBitmap(compositeBitmap, monoBitmapBytes(maxTargetWidth, static_cast<uint16_t>(std::min<uint32_t>(
                                                                                      totalDrawHeight, 4096U)))))
            return false;
    }

    const int32_t maxPanX = (maxTargetWidth > maxWidth) ? static_cast<int32_t>((maxTargetWidth - maxWidth) / 2U) : 0;
    const int32_t maxPanY = (totalDrawHeight > maxHeight) ? static_cast<int32_t>((totalDrawHeight - maxHeight) / 2U) : 0;
    const int32_t clampedPanX = std::clamp<int32_t>(panX, -maxPanX, maxPanX);
    const int32_t clampedPanY = std::clamp<int32_t>(panY, -maxPanY, maxPanY);
    int16_t currentTop =
        static_cast<int16_t>(static_cast<int32_t>(top) + ((static_cast<int32_t>(maxHeight) - totalDrawHeight) / 2) - clampedPanY);
    uint32_t compositeTop = 0U;
    for (size_t i = 0; i < imagePaths.size(); i++) {
        const ImageProbeResult &probe = probes[i];
        const uint16_t targetHeight = targetHeights[i];
        const uint16_t targetWidth =
            std::max<uint16_t>(1U, static_cast<uint16_t>((static_cast<uint64_t>(probe.width) * scaleNum) / scaleDen));
        const int16_t currentLeft =
            static_cast<int16_t>(static_cast<int32_t>(left) + ((static_cast<int32_t>(maxWidth) - targetWidth) / 2) - clampedPanX);
        std::vector<uint8_t> childBitmap;
        uint16_t childWidth = 0;
        uint16_t childHeight = 0;
        std::vector<uint8_t> *childBitmapPtr = captureCacheBitmap ? &childBitmap : nullptr;
        if (!drawInlineImage(imagePaths[i], currentLeft, currentTop, targetWidth, targetHeight, IMAGE_ZOOM_NORMAL, 0, 0,
                             childBitmapPtr, false, false, false, -1, nullptr, &childWidth, &childHeight)) {
            BR_PERF_LOG("drawInlineImageSequence draw-failed path=%s", imagePaths[i].c_str());
            return false;
        }
        if (captureCacheBitmap && !childBitmap.empty() && compositeTop < 4096U) {
            const int16_t compositeLeft = static_cast<int16_t>((static_cast<int32_t>(maxTargetWidth) - childWidth) / 2);
            copyMonoBitmapInto(childBitmap, childWidth, childHeight, compositeBitmap, maxTargetWidth,
                               static_cast<uint16_t>(std::min<uint32_t>(totalDrawHeight, 4096U)), compositeLeft,
                               static_cast<int16_t>(compositeTop + (targetHeight - childHeight) / 2U));
        }
        currentTop = static_cast<int16_t>(currentTop + targetHeight);
        compositeTop += targetHeight;
    }
    if (captureCacheBitmap && !compositeBitmap.empty()) {
        const uint16_t cacheHeight = static_cast<uint16_t>(std::min<uint32_t>(totalDrawHeight, 4096U));
        storeRenderedImageCache(cacheKey, maxTargetWidth, cacheHeight, std::move(compositeBitmap));
        [[maybe_unused]] RenderedImageCache *cache = findRenderedImageCache(cacheKey);
        if (cache && requestedZoom == IMAGE_ZOOM_NORMAL && wrappedLines.size() == 1U && scrollLine == 0) {
            cache->partIndex = pagedPartIndex;
            cache->viewportWidth = width();
            cache->viewportHeight = height();
        }
        BR_PERF_LOG("drawInlineImageSequence cached key=%s size=%ux%u bytes=%u", cacheKey.c_str(),
                    static_cast<unsigned>(maxTargetWidth), static_cast<unsigned>(cacheHeight),
                    static_cast<unsigned>(cache ? cache->bitmap.size() : 0U));
        // Display the newly cached composite through the same cropped fast path
        // used by pan/zoom.
        drawRenderedImageCache(this, cacheKey, left, top, maxWidth, maxHeight, requestedZoom, panX, panY);
    }
    noteBookReaderLoadActivity(true);
    return true;
#endif
}

int32_t InkHUD::BookReaderApplet::getCurrentChapterIndex() const
{
    int32_t current = 0;
    for (size_t i = 0; i < contents.size(); ++i) {
        const auto &entry = contents[i];
        if (entry.part < pagedPartIndex ||
            (entry.part == pagedPartIndex && entry.line >= 0 &&
             entry.line / std::max<int32_t>(1, linesPerPage) <= scrollLine / std::max<int32_t>(1, linesPerPage)))
            current = i;
    }
    return current;
}

void InkHUD::BookReaderApplet::beginJumpToChapter(int32_t chapterIndex)
{
    if (chapterIndex < 0 || chapterIndex >= static_cast<int32_t>(contents.size()))
        return;
    pendingChapterIndex = chapterIndex;
    pendingPageDelta = 0;
    lastReaderInputMs = Time::skipZero(Time::getMillis());
    pendingOpenIndex = -1;
    openingSplashRendered = false;
    mode = Mode::OPENING;
    status = "Loading...";
    noteBookReaderLoadActivity(true);
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

bool InkHUD::BookReaderApplet::jumpToChapter(int32_t chapterIndex)
{
    if (contents.empty())
        return false;
    const int32_t target = std::clamp<int32_t>(chapterIndex, 0, static_cast<int32_t>(contents.size()) - 1);
    const int32_t targetPart = contents[target].part;
    if (targetPart < 0 || targetPart >= static_cast<int32_t>(pagedParts.size()))
        return false;
    const bool chapterStart = contents[target].fragment.empty() || contents[target].offset == 0;
    const bool reuseCurrentPart =
        fileLoaded && targetPart == pagedPartIndex && (chapterStart || (!openFileTruncated && !deferredWrapExpansion));
    if (!reuseCurrentPart && !loadPagedPart(targetPart, false, 0U, chapterStart))
        return false;
    const auto &entry = contents[target];
    if (entry.line < 0) {
        status = "Section location unavailable";
        LOG_WARN("BookReader: missing section anchor %s#%s", entry.path.c_str(), entry.fragment.c_str());
        return false;
    }
    const int32_t step = std::max<int32_t>(1, linesPerPage);
    scrollLine = (entry.line / step) * step;
    chapterPickerSelected = target;
    return true;
}

bool InkHUD::BookReaderApplet::openChapterPicker()
{
    if (contents.size() <= 1) {
        return false;
    }

    if (useTouchFriendlyBookReaderLayout(inkhud)) {
        chapterPickerSelected = 0;
        chapterPickerFirstVisible = 0;
        chapterCursorShown = false;
        hideTouchChapterSelectionHighlight = true;
    } else {
        chapterPickerSelected = getCurrentChapterIndex();
        chapterPickerFirstVisible = std::max<int32_t>(0, chapterPickerSelected - 2);
        chapterCursorShown = true;
        hideTouchChapterSelectionHighlight = false;
    }
    mode = Mode::CHAPTERS;
    return true;
}

void InkHUD::BookReaderApplet::closeChapterPicker()
{
    if (mode == Mode::CHAPTERS) {
        mode = Mode::READER;
    }
}

uint16_t InkHUD::BookReaderApplet::libraryRowHeight() const
{
    return useTouchFriendlyBookReaderLayout(inkhud)
               ? std::max<uint16_t>(BROWSER_ROW_HEIGHT_MIN, fontSmall.lineHeight() * 3U + 20U)
               : std::max<uint16_t>(BROWSER_ROW_HEIGHT_MIN, fontSmall.lineHeight() + 14U);
}

void InkHUD::BookReaderApplet::renderListScrollbar(int32_t count, int32_t first, int32_t rows, uint16_t top, uint16_t areaHeight)
{
    if (count <= rows || !areaHeight || width() <= 14)
        return;
    const uint16_t thumbHeight = std::min<uint16_t>(areaHeight, std::max<int32_t>(12, areaHeight * rows / count));
    const int32_t maxFirst = count - rows;
    const int16_t thumbTop = top + (areaHeight - thumbHeight) * std::clamp<int32_t>(first, 0, maxFirst) / maxFirst;
    fillRect(width() - 14, top, 14, areaHeight, WHITE);
    drawLine(width() - 8, top, width() - 8, top + areaHeight - 1, BLACK);
    fillRect(width() - 11, thumbTop, 6, thumbHeight, BLACK);
}

void InkHUD::BookReaderApplet::renderBrowser()
{
    renderReaderHeader("Back");
    setCrop(HEADER_BUTTON_BACK_WIDTH + 8, 0,
            width() > HEADER_BUTTON_BACK_WIDTH + HEADER_BUTTON_CHAPTER_WIDTH + 16
                ? width() - HEADER_BUTTON_BACK_WIDTH - HEADER_BUTTON_CHAPTER_WIDTH - 16
                : 1,
            getHeaderHeight());
    printAt(width() / 2, getHeaderHeight() / 2, currentDir == BOOKS_DIR ? "Library" : currentDir, CENTER, MIDDLE);
    resetCrop();

    const uint16_t headerBottom = getHeaderHeight();
    const bool touchFriendlyLayout = useTouchFriendlyBookReaderLayout(inkhud);
    const uint16_t rowHeight = libraryRowHeight();
    const uint16_t footerHeight = fontSmall.lineHeight() + 5;
    const int16_t footerTop = (height() > footerHeight) ? (height() - footerHeight) : headerBottom;
    const uint16_t contentTop = headerBottom + 2;
    const uint16_t contentHeight = (footerTop > contentTop) ? (footerTop - contentTop - 1) : 0;
    const int32_t rowsVisible = std::max<int32_t>(1, contentHeight / rowHeight);

    if (touchFriendlyLayout && hideTouchBrowserSelectionHighlight) {
        const int32_t maxFirst = std::max<int32_t>(0, static_cast<int32_t>(entries.size()) - rowsVisible);
        firstVisibleIndex = std::clamp<int32_t>(firstVisibleIndex, 0, maxFirst);
    } else {
        ensureSelectionVisible(selectedIndex, firstVisibleIndex, rowsVisible);
    }

    setCrop(0, contentTop, width(), contentHeight);
    if (entries.empty()) {
        printWrapped(0, contentTop, width(), status.empty() ? "No book files found." : status);
    } else {
        for (int32_t row = 0; row < rowsVisible; row++) {
            const int32_t idx = firstVisibleIndex + row;
            if (idx < 0 || idx >= static_cast<int32_t>(entries.size())) {
                break;
            }

            const int16_t rowTop = contentTop + static_cast<int16_t>(row * rowHeight);
            const BrowserEntry &entry = entries[static_cast<size_t>(idx)];
            const std::string title = entry.isDir ? entry.name : titleFromPath(entry.name);

            const bool showSelection =
                browserCursorShown && (idx == selectedIndex) && (!touchFriendlyLayout || !hideTouchBrowserSelectionHighlight);
            setTextColor(BLACK);
            if (touchFriendlyLayout) {
                const uint16_t textWidth = width() > 52 ? width() - 52 : 1;
                const uint16_t titleHeight = fontSmall.lineHeight() * 2U;
                if (showSelection)
                    fillRect(4, rowTop + 10, 4, rowHeight - 22, BLACK);
                drawLine(16, rowTop + rowHeight - 2, std::max<int16_t>(16, width() - 20), rowTop + rowHeight - 2, BLACK);
                setCrop(16, rowTop + 10, textWidth + 1, titleHeight);
                printWrapped(16, rowTop + 10, textWidth, title);
                printWrapped(17, rowTop + 10, textWidth, title);
                setCrop(0, contentTop, width(), contentHeight);
                std::string detail = "Folder";
                if (!entry.isDir) {
                    const uint32_t megabyte = 1024U * 1024U;
                    const std::string size = entry.size >= megabyte
                                                 ? std::to_string(entry.size / megabyte) + "." +
                                                       std::to_string((entry.size % megabyte) * 10U / megabyte) + " MB"
                                             : entry.size >= 1024U ? std::to_string(entry.size / 1024U) + " KB"
                                                                   : std::to_string(entry.size) + " B";
                    detail = "EPUB  |  " + size;
                }
                setCrop(16, rowTop + rowHeight - fontSmall.lineHeight() - 6, textWidth, fontSmall.lineHeight());
                printAt(16, rowTop + rowHeight - fontSmall.lineHeight() - 6, detail, LEFT, TOP);
                setCrop(0, contentTop, width(), contentHeight);
                printAt(width() - 22, rowTop + rowHeight / 2, ">", CENTER, MIDDLE);
            } else {
                if (showSelection) {
                    fillRect(0, rowTop, width(), rowHeight - 2, BLACK);
                    setTextColor(WHITE);
                }
                printAt(8, rowTop + 6, title, LEFT, TOP);
            }
            setTextColor(BLACK);
        }
    }
    resetCrop();
    const int32_t entryCount = static_cast<int32_t>(entries.size());
    renderListScrollbar(entryCount, firstVisibleIndex, rowsVisible, contentTop, contentHeight);

    for (int16_t x = 0; x < width(); x += 2) {
        drawPixel(x, footerTop, BLACK);
    }

    const int32_t first = entryCount > 0 ? firstVisibleIndex + 1 : 0;
    const int32_t last = std::min(entryCount, firstVisibleIndex + rowsVisible);
    printAt(width() - 8, footerTop + 2, std::to_string(first) + "-" + std::to_string(last) + " / " + std::to_string(entryCount),
            RIGHT, TOP);
}

void InkHUD::BookReaderApplet::renderOpening(bool full)
{
    BR_FLOW_LOG("renderOpening status=%s splashRendered=%u pendingOpenIndex=%ld", status.c_str(),
                static_cast<unsigned>(openingSplashRendered), static_cast<long>(pendingOpenIndex));
    if (pendingPageDelta) {
        if (full)
            renderReader();
    } else if (pendingChapterIndex >= 0 && full)
        renderChapterPicker();
    else if (full)
        renderBrowser();
    renderLoading(0, 0, width(), height());
}

void InkHUD::BookReaderApplet::renderLoading(int16_t left, int16_t top, uint16_t areaWidth, uint16_t areaHeight, bool overlay)
{
    if (!areaWidth || !areaHeight)
        return;
    setFont(fontSmall);
    const int16_t centerX = left + areaWidth / 2;
    const int16_t centerY = top + areaHeight / 2;
    const uint16_t boxWidth = std::min<uint16_t>(180, areaWidth);
    const uint16_t boxHeight = std::min<uint16_t>(104, areaHeight);
    if (overlay) {
        fillRoundRect(centerX - boxWidth / 2, centerY - boxHeight / 2, boxWidth, boxHeight, 6, WHITE);
        drawRoundRect(centerX - boxWidth / 2, centerY - boxHeight / 2, boxWidth, boxHeight, 6, BLACK);
    }
    if (boxHeight >= 72 && boxWidth >= 48) {
        const int16_t iconTop = centerY - 30;
        drawRoundRect(centerX - 20, iconTop, 40, 30, 3, BLACK);
        drawLine(centerX, iconTop, centerX, iconTop + 29, BLACK);
        for (int16_t y = iconTop + 8; y <= iconTop + 20; y += 6) {
            drawLine(centerX - 14, y, centerX - 5, y, BLACK);
            drawLine(centerX + 5, y, centerX + 14, y, BLACK);
        }
        printAt(centerX, centerY + 12, "Loading...", CENTER, TOP);
    } else {
        printAt(centerX, centerY, "Loading...", CENTER, MIDDLE);
    }
}

void InkHUD::BookReaderApplet::renderStyledText(int16_t x, int16_t y, std::string_view text, uint8_t &style, int16_t underlineY)
{
    const int16_t lh = std::max<uint16_t>(1, readingFont().lineHeight());
    std::string segment;
    auto flush = [&]() {
        if (segment.empty())
            return;
        // Sentinel subtraction retains the advance of trailing spaces.
        const uint16_t withSentinel = getTextWidth(segment + "|");
        const uint16_t sentinel = getTextWidth("|");
        const uint16_t advance = withSentinel > sentinel ? withSentinel - sentinel : getTextWidth(segment);
        printAt(x, y, segment, LEFT, TOP);
        if ((style & 1U) && x + 1 < static_cast<int16_t>(width()))
            printAt(x + 1, y, segment, LEFT, TOP);
        const int16_t end = std::min<int16_t>(width() - 1, x + advance - 1);
        if (advance && end >= x) {
            if (style & 2U)
                drawLine(x, underlineY >= 0 ? underlineY : y + lh - 2, end, underlineY >= 0 ? underlineY : y + lh - 2, BLACK);
            if (style & 4U)
                drawLine(x, y + lh / 2, end, y + lh / 2, BLACK);
        }
        x += advance;
        segment.clear();
    };
    for (size_t pos = 0; pos < text.size();) {
        size_t length = 0;
        const auto marker = parseInlineStyleMarkerAt(text, pos, length);
        if (length) {
            flush();
            style = applyInlineStyleMarker(style, marker);
            pos += length;
        } else {
            segment.push_back(text[pos++]);
        }
    }
    flush();
}

void InkHUD::BookReaderApplet::renderReader(bool completeImages, bool imageOnly)
{
#if INKHUD_BOOKREADER_HAS_SD && INKHUD_BOOKREADER_ENABLE_INLINE_IMAGES && INKHUD_BOOKREADER_USE_JPEGDEC
    // Never pause a worker whose image the foreground render might need to collect.
    const std::string &currentText = expandedImageLine.empty() ? displayText : expandedImageLine;
    const bool pausePrefetch = imageZoomPermille > IMAGE_ZOOM_NORMAL && gJpegPrefetchJob && !gJpegPrefetchJob->done.load() &&
                               currentText.find(gJpegPrefetchJob->path) == std::string::npos;
    JpegPrefetchRenderGuard prefetchGuard(pausePrefetch);
    if (pausePrefetch)
        BR_PERF_LOG("JPEG prefetch yielding to zoom render");
#endif
    inlineImageTargets.clear();
    if (!expandedImageLine.empty() && mode == Mode::READER) {
        if (!imageOnly)
            renderReaderHeader("Back");
        const uint16_t top = getHeaderHeight() + 2;
        const uint16_t areaHeight = std::max<int32_t>(1, height() - IMAGE_FOOTER_MIN_HEIGHT - top - 1);
        syncImageViewToCurrentPage();
        setCrop(0, top, width(), areaHeight);
        if (expandedImageLoading) {
            renderLoading(0, top, width(), areaHeight);
            expandedImageLoading = false;
            noteBookReaderLoadActivity(true);
            requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        } else {
            std::string path;
            gImageDrawWidth = gImageDrawHeight = 0;
            const bool rendered = isImageMarkerLine(expandedImageLine, &path) &&
                                  drawInlineImage(path, 0, top, width(), areaHeight, imageZoomPermille, imagePanX, imagePanY,
                                                  nullptr, true, false, true, top + areaHeight);
            if (!rendered)
                printAt(width() / 2, top + areaHeight / 2, "[Image unavailable]", CENTER, MIDDLE);
            if (rendered)
                panImageView(0, 0);
            imageViewportFilled = rendered && gImageDrawWidth >= width() && gImageDrawHeight >= areaHeight;
        }
        resetCrop();
        if (!imageOnly) {
            renderImageToolbar();
            renderReadingProgress();
        }
        lastReaderRenderMs = Time::skipZero(Time::getMillis());
        return;
    }
    [[maybe_unused]] const uint32_t tRenderStart = millis();
    BR_FLOW_LOG("renderReader fileLoaded=%u mode=%u wrappedLines=%u "
                "backgroundPending=%u deferImages=%u scrollLine=%ld",
                static_cast<unsigned>(fileLoaded), static_cast<unsigned>(mode), static_cast<unsigned>(wrappedLines.size()),
                static_cast<unsigned>(backgroundReaderWorkPending), static_cast<unsigned>(deferInlineImagesUntilNextRender),
                static_cast<long>(scrollLine));
    const bool immersiveComicPage = fileLoaded && wrappedLines.size() == 1U &&
                                    (isImageMarkerLine(wrappedLines.front().c_str(), nullptr) ||
                                     isImageSequenceMarkerLine(wrappedLines.front().c_str(), nullptr));
    if (!imageOnly)
        renderReaderHeader(contents.size() > 1 ? "Contents" : nullptr);

    const uint16_t headerBottom = getHeaderHeight();
    const uint16_t baseFooterHeight = ReaderToolbar::progressFooterHeight(fontSmall.lineHeight());
    const uint16_t footerHeight = immersiveComicPage ? IMAGE_FOOTER_MIN_HEIGHT : baseFooterHeight;
    const int16_t footerTop = immersiveComicPage ? static_cast<int16_t>(height() - footerHeight)
                                                 : ((height() > footerHeight) ? (height() - footerHeight) : headerBottom);
    const uint16_t contentTop = static_cast<uint16_t>(headerBottom + 2);
    const uint16_t contentHeight = (footerTop > contentTop) ? (footerTop - contentTop - 1) : 0;

    if (fileLoaded) {
        rewrapIfNeeded(width(), contentHeight);
        ensurePageIndex();
    }
    if (completeImages ||
        (deferInlineImagesUntilNextRender && wrappedLines.size() > 1U && !pageImagesNeedLoading(scrollLine, linesPerPage)))
        deferInlineImagesUntilNextRender = false;

    const bool singleFullPageImage =
        (wrappedLines.size() == 1U) && (isImageMarkerLine(wrappedLines.front().c_str(), nullptr) ||
                                        isImageSequenceMarkerLine(wrappedLines.front().c_str(), nullptr));
    const bool imageViewActive = syncImageViewToCurrentPage();
    const uint16_t renderImageZoom = (imageViewActive && singleFullPageImage) ? imageZoomPermille : IMAGE_ZOOM_NORMAL;
    const int16_t renderImagePanX = (imageViewActive && singleFullPageImage) ? imagePanX : 0;
    const int16_t renderImagePanY = (imageViewActive && singleFullPageImage) ? imagePanY : 0;
    if (singleFullPageImage)
        gImageDrawWidth = gImageDrawHeight = 0;

    setCrop(0, contentTop, width(), contentHeight);
    if (!fileLoaded) {
        const std::string body = status.empty() ? "No file loaded." : status;
        printWrapped(0, contentTop, width(), body);
    } else {
        setFont(readingFont());
        const uint16_t lh = std::max<uint16_t>(1, readingFont().lineHeight());
        uint8_t style = scrollLine >= 0 && static_cast<size_t>(scrollLine) < wrappedLineStyles.size()
                            ? wrappedLineStyles[static_cast<size_t>(scrollLine)]
                            : 0;

        int16_t y = contentTop;
        const int16_t contentBottom = static_cast<int16_t>(contentTop + contentHeight);
        int32_t idx = scrollLine;
        while (idx >= 0 && idx < static_cast<int32_t>(wrappedLines.size()) && y + lh <= contentBottom) {
            const auto &wrapped = wrappedLines[static_cast<size_t>(idx)];
            const std::string_view wrappedLine(wrapped.data(), wrapped.size());
            size_t lineStart = 0;
            uint8_t alignment = 1;
            uint16_t xOffset = consumeLeadingLineXOffsetMarkers(wrappedLine, lineStart, &alignment);
            std::string line(wrappedLine.substr(lineStart));
            const size_t tableMarkerLength = std::strlen(NicheGraphics::InkHUD::TABLE_LINE);
            if (line.compare(0, tableMarkerLength, NicheGraphics::InkHUD::TABLE_LINE) == 0 && line.size() > tableMarkerLength) {
                const unsigned borders = static_cast<unsigned>(line[tableMarkerLength] - '0');
                int16_t cellLeft = 0;
                for (const auto &cell : NicheGraphics::InkHUD::readerTableCells(line)) {
                    char *text = nullptr;
                    const unsigned long cellWidth = std::strtoul(cell.c_str(), &text, 10);
                    if (!text || *text != ':' || !cellWidth || cellWidth > width() || cellLeft + cellWidth > width())
                        break;
                    drawLine(cellLeft, y, cellLeft, y + lh - 1, BLACK);
                    if (borders & 1U)
                        drawLine(cellLeft, y, cellLeft + cellWidth, y, BLACK);
                    if (borders & 2U)
                        drawLine(cellLeft, y + lh - 1, cellLeft + cellWidth, y + lh - 1, BLACK);
                    setCrop(cellLeft + 1, y + 1, cellWidth - 1, lh - 2);
                    uint8_t cellStyle = 0;
                    renderStyledText(cellLeft + 4, y + 1, text + 1, cellStyle, y + lh - 2);
                    setCrop(0, contentTop, width(), contentHeight);
                    cellLeft += cellWidth;
                }
                drawLine(std::min<int16_t>(width() - 1, cellLeft), y, std::min<int16_t>(width() - 1, cellLeft), y + lh - 1,
                         BLACK);
                y += lh;
                ++idx;
                continue;
            }
            xOffset = alignedTextOffset(line, xOffset, alignment);
            const size_t bulletLength = std::strlen(NicheGraphics::InkHUD::BULLET_MARKER);
            if (line.compare(0, bulletLength, NicheGraphics::InkHUD::BULLET_MARKER) == 0) {
                const int16_t gap = std::max<uint16_t>(8, getTextWidth("M"));
                fillCircle(static_cast<int16_t>(xOffset) - gap / 2, y + lh / 2, 2, BLACK);
                line.erase(0, bulletLength);
            }
            std::string imagePath;
            std::vector<std::string> imagePaths;
            const bool singleImage = isImageMarkerLine(line, &imagePath);
            const bool imageSequence = !singleImage && isImageSequenceMarkerLine(line, &imagePaths);
            if (singleImage || imageSequence) {
                int32_t span = 1;
                while (idx + span < static_cast<int32_t>(wrappedLines.size()) &&
                       isImageContinuationLine(wrappedLines[static_cast<size_t>(idx + span)].c_str())) {
                    span++;
                }
                const uint16_t blockHeight = singleFullPageImage ? std::max<uint16_t>(lh, contentHeight)
                                                                 : std::max<uint16_t>(lh, static_cast<uint16_t>(span * lh));
                const uint16_t imageWidth = (width() > xOffset) ? (width() - xOffset) : width();
                const bool tryDeclaredCover = !deferInlineImagesUntilNextRender && singleFullPageImage && pagedPartIndex == 0 &&
                                              !epubCoverImagePath.empty() && !useEpubCoverForFirstPart;
                bool declaredCoverRendered = false;
                if (tryDeclaredCover) {
                    BR_PERF_LOG("first page: trying declared cover path=%s", epubCoverImagePath.c_str());
                    declaredCoverRendered =
                        drawInlineImage(epubCoverImagePath, static_cast<int16_t>(xOffset), y, imageWidth, blockHeight,
                                        renderImageZoom, renderImagePanX, renderImagePanY, nullptr, true);
                    if (declaredCoverRendered) {
                        useEpubCoverForFirstPart = true;
                        displayText = makeImageMarkerLine(epubCoverImagePath);
                        wrappedLines.front() = displayText.c_str();
                        imageViewKey = openFilePath + "#" + std::to_string(pagedPartIndex) + "#" + displayText;
                    }
                }
                if (deferInlineImagesUntilNextRender) {
                    noteBookReaderLoadActivity(true);
                    renderLoading(static_cast<int16_t>(xOffset), y, imageWidth,
                                  std::min<uint16_t>(blockHeight, contentBottom - y), false);
                } else if (!declaredCoverRendered &&
                           !(imageSequence
                                 ? drawInlineImageSequence(imagePaths, static_cast<int16_t>(xOffset), y, imageWidth, blockHeight,
                                                           renderImageZoom, renderImagePanX, renderImagePanY, singleFullPageImage)
                                 : drawInlineImage(imagePath, static_cast<int16_t>(xOffset), y, imageWidth, blockHeight,
                                                   renderImageZoom, renderImagePanX, renderImagePanY, nullptr, true, false,
                                                   !singleFullPageImage, contentBottom))) {
                    printAt(static_cast<int16_t>(xOffset), y, "[Image unavailable]", LEFT, TOP);
                }
                if (singleImage && !singleFullPageImage && !deferInlineImagesUntilNextRender) {
                    const auto probe = probeImageFile(imagePath);
                    if (probe.valid && probe.width && probe.height) {
                        uint32_t hitWidth = imageWidth;
                        uint32_t hitHeight = hitWidth * probe.height / probe.width;
                        if (hitHeight > blockHeight) {
                            hitHeight = blockHeight;
                            hitWidth = hitHeight * probe.width / probe.height;
                        }
                        const int16_t hitTop = y + (blockHeight - hitHeight) / 2;
                        inlineImageTargets.push_back(
                            {line, static_cast<int16_t>(xOffset + (imageWidth - hitWidth) / 2), hitTop,
                             static_cast<uint16_t>(hitWidth),
                             static_cast<uint16_t>(std::max<int32_t>(0, std::min<int32_t>(hitHeight, contentBottom - hitTop)))});
                    }
                }
                y += static_cast<int16_t>(blockHeight);
                idx += span;
                continue;
            }

            if (isImageContinuationLine(line)) {
                y += static_cast<int16_t>(lh);
                idx++;
                continue;
            }

            renderStyledText(static_cast<int16_t>(xOffset), y, line, style);
            y += static_cast<int16_t>(lh);
            idx++;
        }
    }
    resetCrop();

    setFont(fontSmall);
    if (imageViewActive && gImageDrawWidth && gImageDrawHeight)
        panImageView(0, 0);
    imageViewportFilled = singleFullPageImage && !deferInlineImagesUntilNextRender && gImageDrawWidth >= width() &&
                          gImageDrawHeight >= contentHeight;
    if (!imageOnly && immersiveComicPage) {
        renderImageToolbar();
    } else if (!imageOnly) {
        for (int16_t x = 0; x < width(); x += 2) {
            drawPixel(x, footerTop, BLACK);
        }
    }

    if (!imageOnly)
        renderReadingProgress();

    BR_PERF_LOG("renderReader cpu=%lums wrappedLines=%u", static_cast<unsigned long>(millis() - tRenderStart),
                static_cast<unsigned>(wrappedLines.size()));
    BR_PERF_LOG("page CPU finished part=%ld line=%ld pendingWork=%u pendingImages=%u ready=%u", static_cast<long>(pagedPartIndex),
                static_cast<long>(scrollLine), static_cast<unsigned>(backgroundReaderWorkPending),
                static_cast<unsigned>(deferInlineImagesUntilNextRender),
                static_cast<unsigned>(!backgroundReaderWorkPending && !deferInlineImagesUntilNextRender));

    if (backgroundReaderWorkPending) {
        backgroundReaderWorkArmed = false;
    }
    if (deferInlineImagesUntilNextRender) {
        deferInlineImagesUntilNextRender = false;
        noteBookReaderLoadActivity(true);
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
    }
    lastReaderRenderMs = Time::skipZero(Time::getMillis());
}

void InkHUD::BookReaderApplet::renderChapterPicker()
{
    renderReaderHeader("Back");
    printAt(width() / 2, getHeaderHeight() / 2, "Contents", CENTER, MIDDLE);

    const uint16_t headerBottom = getHeaderHeight();
    const bool touchFriendlyLayout = useTouchFriendlyBookReaderLayout(inkhud);
    const uint16_t rowHeight =
        touchFriendlyLayout ? std::max<uint16_t>(BROWSER_ROW_HEIGHT_MIN, static_cast<uint16_t>(fontMedium.lineHeight() * 1.9f))
                            : std::max<uint16_t>(BROWSER_ROW_HEIGHT_MIN, static_cast<uint16_t>(fontSmall.lineHeight() + 12));
    const uint16_t footerHeight = fontSmall.lineHeight() + 5;
    const int16_t footerTop = (height() > footerHeight) ? (height() - footerHeight) : headerBottom;
    const uint16_t contentTop = headerBottom + 2;
    const uint16_t contentHeight = (footerTop > contentTop) ? (footerTop - contentTop - 1) : 0;
    const int32_t rowsVisible = std::max<int32_t>(1, contentHeight / rowHeight);

    const int32_t chapterCount = static_cast<int32_t>(contents.size());
    if (chapterPickerSelected < 0) {
        chapterPickerSelected = 0;
    }
    if (chapterPickerSelected >= chapterCount) {
        chapterPickerSelected = chapterCount - 1;
    }

    if (touchFriendlyLayout && hideTouchChapterSelectionHighlight) {
        // Preserve the swipe-scrolled window while the selection is hidden.
        const int32_t maxFirst = std::max<int32_t>(0, chapterCount - rowsVisible);
        chapterPickerFirstVisible = std::clamp<int32_t>(chapterPickerFirstVisible, 0, maxFirst);
    } else {
        ensureSelectionVisible(chapterPickerSelected, chapterPickerFirstVisible, rowsVisible);
    }

    setCrop(0, contentTop, width(), contentHeight);
    for (int32_t row = 0; row < rowsVisible; row++) {
        const int32_t idx = chapterPickerFirstVisible + row;
        if (idx < 0 || idx >= static_cast<int32_t>(contents.size())) {
            break;
        }

        const int16_t rowTop = contentTop + static_cast<int16_t>(row * rowHeight);
        const bool showSelection =
            chapterCursorShown && (idx == chapterPickerSelected) && (!touchFriendlyLayout || !hideTouchChapterSelectionHighlight);
        if (showSelection) {
            fillRect(0, rowTop, width(), rowHeight - 2, BLACK);
            setTextColor(WHITE);
        } else {
            setTextColor(BLACK);
            for (int16_t x = 0; x < width(); x += 4) {
                drawPixel(x, rowTop + rowHeight - 2, BLACK);
            }
        }

        const int16_t indent =
            static_cast<size_t>(idx) < contents.size() ? std::min<int16_t>(contents[idx].depth * 16, width() / 3) : 0;
        setCrop(8 + indent, rowTop, width() > 30 + indent ? width() - 30 - indent : 1, rowHeight - 2);
        std::string label = parse(contents[static_cast<size_t>(idx)].title);
        const int32_t available = std::max<int32_t>(1, width() - 30 - indent);
        if (getTextWidth(label) > available) {
            while (!label.empty() && getTextWidth(label + "...") > available)
                label.pop_back();
            label += "...";
        }
        printAt(8 + indent, rowTop + 6, label, LEFT, TOP);
        setCrop(0, contentTop, width(), contentHeight);
        setTextColor(BLACK);
    }
    resetCrop();

    renderListScrollbar(chapterCount, chapterPickerFirstVisible, rowsVisible, contentTop, contentHeight);
    setFont(fontSmall);
    for (int16_t x = 0; x < width(); x += 2) {
        drawPixel(x, footerTop, BLACK);
    }
    const int32_t first = chapterCount > 0 ? chapterPickerFirstVisible + 1 : 0;
    const int32_t last = std::min(chapterCount, chapterPickerFirstVisible + rowsVisible);
    printAt(width() - 8, footerTop + 2,
            std::to_string(first) + "-" + std::to_string(last) + " / " + std::to_string(chapterCount) + " entries", RIGHT, TOP);
}

void InkHUD::BookReaderApplet::onRender(bool full)
{
    const bool imageOnly = !full && imagePanOnlyRender && imageViewportFilled && mode == Mode::READER;
    imagePanOnlyRender = false;
    imageViewportFilled = false;
    imageInteractionPending = false;
    BR_LOAD_SCOPE("page-cpu-render");
    BR_PERF_LOG("page state mode=%u initialized=%u loaded=%u part=%ld line=%ld deferred=%u", static_cast<unsigned>(mode),
                static_cast<unsigned>(initialized), static_cast<unsigned>(fileLoaded), static_cast<long>(pagedPartIndex),
                static_cast<long>(scrollLine), static_cast<unsigned>(backgroundReaderWorkPending));
    if (imageOnly) {
        BR_PERF_LOG("pan render: retaining header and controls");
        renderReader(false, true);
        return;
    }

    if (mode == Mode::MENU) {
        renderReaderMenu();
        return;
    }

    if (!initialized && !initialLoadArmed) {
        initialLoadArmed = true;
        renderLoading(0, 0, width(), height());
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        return;
    }

    ensureInitialDirectory();
    initialLoadArmed = false;
    BR_FLOW_LOG("onRender mode=%u full=%u pendingOpenIndex=%ld fileLoaded=%u "
                "backgroundPending=%u openingSplashRendered=%u",
                static_cast<unsigned>(mode), static_cast<unsigned>(full), static_cast<long>(pendingOpenIndex),
                static_cast<unsigned>(fileLoaded), static_cast<unsigned>(backgroundReaderWorkPending),
                static_cast<unsigned>(openingSplashRendered));

    if (mode == Mode::OPENING) {
        if (!openingSplashRendered) {
            renderOpening(full);
            openingSplashRendered = true;
            BR_FLOW_LOG("onRender openingSplashShown");
            requestUpdate(Drivers::EInk::UpdateTypes::FAST);
            return;
        }

        if (pendingPageDelta) {
            const int delta = pendingPageDelta;
            pendingPageDelta = 0;
            mode = Mode::READER;
            status.clear();
            scrollByPages(delta, false);
            openingSplashRendered = false;
            noteBookReaderLoadActivity(true);
            renderReader(true);
        } else if (pendingChapterIndex >= 0) {
            const int32_t target = pendingChapterIndex;
            pendingChapterIndex = -1;
            openingSplashRendered = false;
            status.clear();
            const bool ok = jumpToChapter(target);
            mode = ok ? Mode::READER : Mode::CHAPTERS;
            noteBookReaderLoadActivity(true);
            if (ok)
                renderReader(true);
            else
                renderChapterPicker();
        } else if (pendingOpenIndex >= 0 && pendingOpenIndex < static_cast<int32_t>(entries.size())) {
            noteBookReaderLoadActivity(true);
            selectedIndex = pendingOpenIndex;
            pendingOpenIndex = -1;
            openingSplashRendered = false;
            [[maybe_unused]] const bool ok = openSelectedEntry();
            BR_FLOW_LOG("onRender openSelectedEntryDone ok=%u mode=%u fileLoaded=%u", static_cast<unsigned>(ok),
                        static_cast<unsigned>(mode), static_cast<unsigned>(fileLoaded));
            // Restart timeout countdown now that loading has finished.
            noteBookReaderLoadActivity(true);

            if (mode == Mode::BROWSER) {
                renderBrowser();
            } else if (mode == Mode::CHAPTERS) {
                renderChapterPicker();
            } else {

                renderReader(true);
            }
        } else {
            mode = Mode::BROWSER;
            pendingOpenIndex = -1;
            openingSplashRendered = false;
            BR_FLOW_LOG("onRender openingCancelledFallbackToBrowser");
            renderBrowser();
        }
        return;
    } else if (mode == Mode::BROWSER) {
        renderBrowser();
    } else if (mode == Mode::CHAPTERS) {
        renderChapterPicker();
    } else {

        renderReader();
    }
}

void InkHUD::BookReaderApplet::onExitShort()
{
    if (!expandedImageLine.empty()) {
        closeExpandedImage();
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        return;
    }
    saveReadingPosition();
    if (mode == Mode::BROWSER) {
        inkhud->openAppSwitcher();
        return;
    }
    pendingOpenIndex = -1;
    openingSplashRendered = false;
    mode = Mode::BROWSER;
    pendingChapterIndex = -1;
    pendingPageDelta = 0;
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

void InkHUD::BookReaderApplet::onButtonShortPress()
{
    if (useTouchFriendlyBookReaderLayout(inkhud)) {
        inkhud->nextApplet();
        return;
    }
    if (mode == Mode::MENU) {
        menuSelection = (menuSelection + 1U) % readerMenuItemCount();
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        return;
    }
    onNavHorizontal(1);
}

void InkHUD::BookReaderApplet::onButtonLongPress()
{
    if (mode == Mode::MENU) {
        selectMenuItem(menuSelection);
        return;
    }
    if (mode == Mode::OPENING) {
        return;
    } else if (mode == Mode::BROWSER) {
        if (useTouchFriendlyBookReaderLayout(inkhud)) {
            if (browserCursorShown && selectedIndex >= 0 && selectedIndex < static_cast<int32_t>(entries.size())) {
                beginOpenSelectedEntry();
                return;
            }
            requestUpdate(Drivers::EInk::UpdateTypes::FAST);
            return;
        }
        beginOpenSelectedEntry();
    } else if (mode == Mode::CHAPTERS) {
        if (useTouchFriendlyBookReaderLayout(inkhud)) {
            if (chapterCursorShown && chapterPickerSelected >= 0 &&
                chapterPickerSelected < static_cast<int32_t>(contents.size())) {
                beginJumpToChapter(chapterPickerSelected);
                return;
            }
            requestUpdate(Drivers::EInk::UpdateTypes::FAST);
            return;
        }
        beginJumpToChapter(chapterPickerSelected);
    } else {
        scrollByPages(1);
    }
    requestUpdate(Drivers::EInk::UpdateTypes::FAST, pendingPageDelta == 0);
}

void InkHUD::BookReaderApplet::scrollTouchList(int direction)
{
    const bool browser = mode == Mode::BROWSER;
    auto &first = browser ? firstVisibleIndex : chapterPickerFirstVisible;
    (browser ? hideTouchBrowserSelectionHighlight : hideTouchChapterSelectionHighlight) = true;
    (browser ? browserCursorShown : chapterCursorShown) = false;
    const size_t count = browser ? entries.size() : contents.size();
    if (!count)
        return;
    const uint16_t rowHeight =
        browser ? libraryRowHeight()
                : std::max<uint16_t>(BROWSER_ROW_HEIGHT_MIN, static_cast<uint16_t>(fontMedium.lineHeight() * 1.9f));
    const uint16_t headerBottom = getHeaderHeight();
    const uint16_t footerHeight = fontSmall.lineHeight() + 5;
    const int16_t footerTop = height() > footerHeight ? height() - footerHeight : headerBottom;
    const uint16_t contentTop = headerBottom + 2;
    const uint16_t contentHeight = footerTop > contentTop ? footerTop - contentTop - 1 : 0;
    const int32_t pageStep = std::max<int32_t>(1, contentHeight / rowHeight);
    if (direction > 0)
        first = std::min<int32_t>(std::max<int32_t>(0, static_cast<int32_t>(count) - pageStep), first + pageStep);
    else
        first = std::max<int32_t>(0, first - pageStep);
}

void InkHUD::BookReaderApplet::onNavUp()
{
    onNavVertical(-1);
}

void InkHUD::BookReaderApplet::onNavDown()
{
    onNavVertical(1);
}

void InkHUD::BookReaderApplet::onNavVertical(int direction)
{
    if (mode == Mode::MENU) {
        onTouchSwipe(0, direction);
        return;
    }
    if (mode == Mode::OPENING)
        return;
    if (mode == Mode::BROWSER || mode == Mode::CHAPTERS) {
        if (useTouchFriendlyBookReaderLayout(inkhud)) {
            scrollTouchList(-direction);
        } else if (mode == Mode::BROWSER) {
            moveSelection(direction);
        } else {
            const int32_t maxChapter = static_cast<int32_t>(contents.size()) - 1;
            if ((direction < 0 && chapterPickerSelected > 0) || (direction > 0 && chapterPickerSelected < maxChapter))
                chapterPickerSelected += direction;
        }
    } else {
        scrollByPages(direction);
    }
    requestUpdate(Drivers::EInk::UpdateTypes::FAST, pendingPageDelta == 0);
}

void InkHUD::BookReaderApplet::onNavLeft()
{
    onNavHorizontal(-1);
}

void InkHUD::BookReaderApplet::onNavRight()
{
    onNavHorizontal(1);
}

void InkHUD::BookReaderApplet::onNavHorizontal(int direction)
{
    if (mode == Mode::MENU) {
        if (direction > 0) {
            selectMenuItem(menuSelection);
        } else {
            mode = menuReturnMode;
            requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        }
        return;
    }
    if (useTouchFriendlyBookReaderLayout(inkhud) && (mode == Mode::BROWSER || mode == Mode::CHAPTERS)) {
        // Some touch stacks report vertical swipes as LEFT/RIGHT.
        onNavVertical(direction);
        return;
    }
    if (mode == Mode::OPENING)
        return;
    if (mode == Mode::BROWSER) {
        if (direction > 0)
            beginOpenSelectedEntry();
        else
            goToParentDirectory();
    } else if (mode == Mode::CHAPTERS) {
        if (direction > 0)
            beginJumpToChapter(chapterPickerSelected);
        else
            closeChapterPicker();
    } else {
        scrollByPages(direction);
    }
    requestUpdate(Drivers::EInk::UpdateTypes::FAST, pendingPageDelta == 0);
}

void InkHUD::BookReaderApplet::openReaderMenu()
{
    if (mode == Mode::OPENING || mode == Mode::MENU) {
        return;
    }
    closeExpandedImage();
    menuReturnMode = mode;
    mode = Mode::MENU;
    menuSelection = 0;
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

uint8_t InkHUD::BookReaderApplet::readerMenuItemCount() const
{
    return fileLoaded && readingPosition.bookmarkLine >= 0 ? READER_MENU_ITEMS : READER_MENU_ITEMS - 2;
}

void InkHUD::BookReaderApplet::selectMenuItem(uint8_t item)
{
    if (item >= readerMenuItemCount())
        return;
    mode = menuReturnMode;
    if (item == readerMenuItemCount() - 1) {
        saveReadingPosition();
        inkhud->openAppSwitcher();
    } else if (item == 0 && fileLoaded) {
        mode = Mode::READER;
    } else if (item == 1) {
        saveReadingPosition();
        mode = Mode::BROWSER;
        if (useTouchFriendlyBookReaderLayout(inkhud)) {
            browserCursorShown = false;
            hideTouchBrowserSelectionHighlight = true;
        }
    } else if (item == 2) {
        if (!fileLoaded || !openChapterPicker()) {
            mode = Mode::MENU;
        }
    } else if (item == 3 && fileLoaded) {
        readingPosition.bookmarkPart = pagedPartIndex;
        readingPosition.bookmarkLine = scrollLine;
        readingPosition.bookmarkTextSize = 0;
        saveReadingPosition();
        mode = Mode::MENU;
    } else if (item == 4 && fileLoaded && readingPosition.bookmarkLine >= 0) {
        mode = Mode::READER;
        restoreLocation(readingPosition.bookmarkPart, readingPosition.bookmarkLine);
    } else if (item == 5) {
        readingPosition.bookmarkPart = -1;
        readingPosition.bookmarkLine = -1;
        readingPosition.bookmarkTextSize = 0;
        saveReadingPosition();
        menuSelection = 3;
        mode = Mode::MENU;
    }
    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
}

void InkHUD::BookReaderApplet::renderReaderMenu()
{
    drawHeader("Book Reader");
    if (fileLoaded) {
        const std::string progress = readingProgress();
        setFont(fontSmall);
        printAt(width() - 12, getHeaderHeight() + 2, progress, RIGHT, TOP);
    }
    const uint16_t top = getHeaderHeight() + fontSmall.lineHeight() + 12;
    const uint8_t count = readerMenuItemCount();
    menuSelection = std::min<uint8_t>(menuSelection, count - 1);
    const uint16_t rowHeight = std::min<uint16_t>(72, std::max<int32_t>(1, (height() - top - 8) / count));
    const std::array<std::string, READER_MENU_ITEMS> labels = {
        "Resume",
        "Library",
        "Contents",
        (readingPosition.bookmarkPart == pagedPartIndex && readingPosition.bookmarkLine == scrollLine) ? "Page bookmarked"
                                                                                                       : "Bookmark this page",
        count == READER_MENU_ITEMS ? "Go to bookmark" : "Exit to applets",
        "Remove bookmark",
        "Exit to applets"};
    setFont(rowHeight >= fontMedium.lineHeight() + 10 ? fontMedium : fontSmall);
    for (uint8_t i = 0; i < count; ++i) {
        const int16_t y = top + i * rowHeight;
        if (i == menuSelection) {
            fillRect(0, y, 4, rowHeight - 2, BLACK);
        }
        setCrop(12, y, width() > 24 ? width() - 24 : 1, rowHeight);
        printAt(20, y + rowHeight / 2, labels[i], LEFT, MIDDLE);
        resetCrop();
        drawLine(12, y + rowHeight - 1, width() - 12, y + rowHeight - 1, BLACK);
    }
    setFont(fontSmall);
}

void InkHUD::BookReaderApplet::renderReaderHeader(const char *rightLabel)
{
    drawHeader("");
    for (uint8_t i = 0; i < (rightLabel ? 2U : 1U); ++i) {
        int16_t left = 0, top = 0;
        uint16_t w = 0, h = 0;
        getHeaderButtonRect(width(), getHeaderHeight(), i != 0, i == 0 ? HEADER_BUTTON_BACK_WIDTH : HEADER_BUTTON_CHAPTER_WIDTH,
                            left, top, w, h);
        fillRect(left, top, w, h, WHITE);
        drawRoundRect(left, top, w, h, 6, BLACK);
        printAt(left + w / 2, top + h / 2, i == 0 ? "Menu" : rightLabel, CENTER, MIDDLE);
    }
}

void InkHUD::BookReaderApplet::renderImageToolbar()
{
    const int16_t top = height() - IMAGE_FOOTER_MIN_HEIGHT;
    fillRect(0, top, width(), IMAGE_FOOTER_MIN_HEIGHT, WHITE);
    drawLine(0, top, width() - 1, top, BLACK);
    const uint32_t total = pageNavigation ? pagedParts.size() : readerPagePrefix(partPageCounts, pagedParts.size());
    const uint32_t prefix = pageNavigation ? std::max<int32_t>(0, pagedPartIndex)
                                           : readerPagePrefix(partPageCounts, std::max<int32_t>(0, pagedPartIndex));
    if (pagedBookMode && total > 0) {
        const uint16_t progressWidth = static_cast<uint32_t>(width()) * (prefix + 1) / total;
        fillRect(0, top, progressWidth, 3, BLACK);
    }
    const std::array<std::string, ReaderToolbar::count> labels = {"<", "-", std::to_string(imageZoomPermille / 10) + "%", "+",
                                                                  ">"};
    for (uint8_t i = 0; i < labels.size(); ++i) {
        const int16_t left = ReaderToolbar::boundary(width(), i);
        const int16_t right = ReaderToolbar::boundary(width(), i + 1);
        const int16_t buttonHeight =
            std::min<int16_t>(52, std::max<int16_t>(20, IMAGE_FOOTER_MIN_HEIGHT -
                                                            ReaderToolbar::progressFooterHeight(fontSmall.lineHeight()) - 18));
        drawRoundRect(left + 4, top + 10, std::max<int16_t>(1, right - left - 8), buttonHeight, 8, BLACK);
        setCrop(left + 2, top + 1, std::max<int16_t>(1, right - left - 4), IMAGE_FOOTER_MIN_HEIGHT - 1);
        printAt((left + right) / 2, top + 10 + buttonHeight / 2, labels[i], CENTER, MIDDLE);
        resetCrop();
    }
}

bool InkHUD::BookReaderApplet::onTouchSwipe(int8_t dx, int8_t dy)
{
    lastReaderInputMs = Time::skipZero(Time::getMillis());
    noteBookReaderLoadActivity(true);
    if (mode == Mode::OPENING) {
        return true;
    }
    if (mode == Mode::MENU) {
        menuSelection = std::clamp<int>(menuSelection + ((dy > 0 || dx > 0) ? 1 : -1), 0, readerMenuItemCount() - 1);
    } else if (mode == Mode::READER) {
        if (syncImageViewToCurrentPage() && imageZoomPermille > IMAGE_ZOOM_NORMAL) {
            if (!panImageView(-dx * static_cast<int16_t>(width() / 3),
                              -dy * static_cast<int16_t>(
                                        std::max<int32_t>(1, height() - IMAGE_FOOTER_MIN_HEIGHT - getHeaderHeight() - 3) / 3))) {
                return true;
            }
            queueImageInteractionUpdate(true);
            return true;
        } else {
            scrollByPages((dx < 0 || dy < 0) ? 1 : -1);
        }
    } else {
        if (dx < 0 || dy < 0) {
            onNavUp();
        } else {
            onNavDown();
        }
        return true;
    }
    requestUpdate(Drivers::EInk::UpdateTypes::FAST, pendingPageDelta == 0);
    return true;
}

bool InkHUD::BookReaderApplet::onTouchPoint(uint16_t x, uint16_t y, bool longPress)
{
    lastReaderInputMs = Time::skipZero(Time::getMillis());
    if (getTile()) {
        const int32_t localX = static_cast<int32_t>(x) - getTile()->getLeft();
        const int32_t localY = static_cast<int32_t>(y) - getTile()->getTop();
        if (localX < 0 || localY < 0 || localX >= width() || localY >= height()) {
            if (!longPress && mode == Mode::READER && imageZoomPermille == IMAGE_ZOOM_NORMAL &&
                (localX < 0 || localX >= width()) && localY >= getHeaderHeight() && localY < height()) {
                scrollByPages(localX < 0 ? -1 : 1);
                requestUpdate(Drivers::EInk::UpdateTypes::FAST, pendingPageDelta == 0);
            }
            // Border taps must not fall back to the physical-button applet switch.
            return true;
        }
        x = localX;
        y = localY;
    }
    const uint16_t headerH = getHeaderHeight();

    if (mode == Mode::OPENING) {
        return true;
    }

    if (longPress)
        return true;

    if (mode == Mode::MENU) {
        const uint16_t top = headerH + fontSmall.lineHeight() + 12;
        const uint8_t count = readerMenuItemCount();
        const uint16_t rowHeight = std::min<uint16_t>(72, std::max<int32_t>(1, (height() - top - 8) / count));
        if (y >= top && y < top + rowHeight * count) {
            selectMenuItem((y - top) / rowHeight);
        }
        return true;
    }

    if (mode == Mode::READER && y < headerH) {
        int16_t left = 0, top = 0;
        uint16_t w = 0, h = 0;
        getHeaderButtonRect(width(), headerH, false, HEADER_BUTTON_BACK_WIDTH, left, top, w, h);
        if (pointInRect(x, y, left, top, w, h))
            openReaderMenu();
        else {
            getHeaderButtonRect(width(), headerH, true, HEADER_BUTTON_CHAPTER_WIDTH, left, top, w, h);
            if (pointInRect(x, y, left, top, w, h)) {
                if (!expandedImageLine.empty()) {
                    closeExpandedImage();
                    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
                } else if (openChapterPicker()) {
                    requestUpdate(Drivers::EInk::UpdateTypes::FAST);
                }
            }
        }
        return true;
    }

    if (currentImagePageLine(nullptr)) {
        const uint16_t toolbarTop = height() - IMAGE_FOOTER_MIN_HEIGHT;
        if (y >= toolbarTop) {
            const uint8_t item = ReaderToolbar::hit(width(), x);
            switch (item) {
            case 0:
                scrollByPages(-1);
                break;
            case 1:
                if (cycleImageZoom(true))
                    queueImageInteractionUpdate();
                return true;
            case 2:
                if (imageZoomPermille == IMAGE_ZOOM_NORMAL && imagePanX == 0 && imagePanY == 0)
                    return true;
                imageZoomPermille = IMAGE_ZOOM_NORMAL;
                imagePanX = imagePanY = 0;
                queueImageInteractionUpdate();
                return true;
            case 3:
                if (cycleImageZoom(false))
                    queueImageInteractionUpdate();
                return true;
            case 4:
                scrollByPages(1);
                break;
            }
        } else if (imageZoomPermille == IMAGE_ZOOM_NORMAL && (x < width() / 3 || x >= width() * 2 / 3)) {
            scrollByPages(x < width() / 3 ? -1 : 1);
        } else {
            return true;
        }
        requestUpdate(Drivers::EInk::UpdateTypes::FAST, pendingPageDelta == 0);
        return true;
    }

    if (mode == Mode::BROWSER) {
        const bool touchFriendlyLayout = useTouchFriendlyBookReaderLayout(inkhud);
        if (touchFriendlyLayout) {
            browserCursorShown = false;
            hideTouchBrowserSelectionHighlight = true;
        }
        const uint16_t rowHeight = libraryRowHeight();
        const uint16_t contentTop = headerH + 2;

        if (y < headerH) {
            int16_t left = 0, top = 0;
            uint16_t w = 0, h = 0;
            getHeaderButtonRect(width(), headerH, false, HEADER_BUTTON_BACK_WIDTH, left, top, w, h);
            if (pointInRect(x, y, left, top, w, h)) {
                openReaderMenu();
            } else {
                getHeaderButtonRect(width(), headerH, true, HEADER_BUTTON_CHAPTER_WIDTH, left, top, w, h);
                if (pointInRect(x, y, left, top, w, h))
                    goToParentDirectory();
            }
            requestUpdate(Drivers::EInk::UpdateTypes::FAST);
            return true;
        }

        if (y >= contentTop) {
            const uint16_t footerHeight = fontSmall.lineHeight() + 5;
            const int16_t footerTop = height() > footerHeight ? height() - footerHeight : headerH;
            const int32_t contentHeight = std::max<int32_t>(0, footerTop - contentTop - 1);
            const int32_t rowsVisible = std::max<int32_t>(1, contentHeight / rowHeight);
            if (y >= contentTop + rowsVisible * rowHeight || x >= width() - std::min<uint16_t>(14, width()))
                return true;
            const int32_t row = static_cast<int32_t>((y - contentTop) / rowHeight);
            const int32_t idx = firstVisibleIndex + row;
            if (idx >= 0 && idx < static_cast<int32_t>(entries.size())) {
                selectedIndex = idx;
                browserCursorShown = true;
                hideTouchBrowserSelectionHighlight = false;
                beginOpenSelectedEntry();
                return true;
            }
        }
        // Consume taps on browser whitespace so they don't fall back to button
        // semantics.
        return true;
    }

    if (mode == Mode::CHAPTERS) {
        if (useTouchFriendlyBookReaderLayout(inkhud)) {
            chapterCursorShown = false;
            hideTouchChapterSelectionHighlight = true;
        }
        int16_t backLeft = 0;
        int16_t backTop = 0;
        uint16_t backWidth = 0;
        uint16_t backHeight = 0;
        getHeaderButtonRect(width(), headerH, true, HEADER_BUTTON_CHAPTER_WIDTH, backLeft, backTop, backWidth, backHeight);

        if (pointInRect(x, y, backLeft, backTop, backWidth, backHeight)) {
            closeChapterPicker();
            requestUpdate(Drivers::EInk::UpdateTypes::FAST);
            return true;
        }

        if (y < headerH) {
            getHeaderButtonRect(width(), headerH, false, HEADER_BUTTON_BACK_WIDTH, backLeft, backTop, backWidth, backHeight);
            if (pointInRect(x, y, backLeft, backTop, backWidth, backHeight)) {
                openReaderMenu();
                requestUpdate(Drivers::EInk::UpdateTypes::FAST);
            }
            return true;
        }

        const bool touchFriendlyLayout = useTouchFriendlyBookReaderLayout(inkhud);
        const uint16_t rowHeight =
            touchFriendlyLayout
                ? std::max<uint16_t>(BROWSER_ROW_HEIGHT_MIN, static_cast<uint16_t>(fontMedium.lineHeight() * 1.9f))
                : std::max<uint16_t>(BROWSER_ROW_HEIGHT_MIN, static_cast<uint16_t>(fontSmall.lineHeight() + 12));
        const uint16_t contentTop = headerH + 2;
        const uint16_t footerHeight = fontSmall.lineHeight() + 5;
        const int32_t contentHeight = std::max<int32_t>(0, static_cast<int32_t>(height()) - footerHeight - contentTop - 1);
        const int32_t rowsVisible = std::max<int32_t>(1, contentHeight / rowHeight);
        if (y < contentTop || y >= contentTop + rowsVisible * rowHeight || x >= width() - std::min<uint16_t>(14, width()))
            return true;
        const int32_t row = static_cast<int32_t>((y - contentTop) / rowHeight);
        const int32_t idx = chapterPickerFirstVisible + row;
        if (idx >= 0 && idx < static_cast<int32_t>(contents.size())) {
            chapterPickerSelected = idx;
            chapterCursorShown = true;
            hideTouchChapterSelectionHighlight = false;
            beginJumpToChapter(chapterPickerSelected);
            return true;
        }

        // Consume taps on chapter-list whitespace so they don't trigger fallback
        // button behavior.
        return true;
    }

    for (const auto &target : inlineImageTargets) {
        if (pointInRect(x, y, target.left, target.top, target.width, target.height)) {
            expandedImageLine = target.line;
            resetImageView();
            expandedImageLoading = true;
            noteBookReaderLoadActivity(true);
            requestUpdate(Drivers::EInk::UpdateTypes::FAST);
            return true;
        }
    }
    const uint16_t third = std::max<uint16_t>(1, width() / 3);
    if (x < third || x >= third * 2) {
        scrollByPages(x < third ? -1 : 1);
        requestUpdate(Drivers::EInk::UpdateTypes::FAST, pendingPageDelta == 0);
    }
    return true;
}

#endif
