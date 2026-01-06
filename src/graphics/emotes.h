#pragma once
#include <Arduino.h>

namespace graphics
{

// ============================================================================
// Emote 编译控制宏 - 按含义分类的扩展系统
// ============================================================================
//
// 默认配置: 仅包含基础 105 个手工定义的高质量 emote
//
// 通过定义以下宏来启用更多 emoji 分类:
//
// 基础集: 93 个 (~3162 bytes) - 总是包含
//
// 扩展分类 (按含义分组):
//
// #define EMOTE_INCLUDE_SMILEYS_EMOTION           //   66 个 (~ 2244 bytes) - 笑脸和情感表情
// #define EMOTE_INCLUDE_HAND_FINGERS              //    9 个 (~  306 bytes) - 手势和手指
// #define EMOTE_INCLUDE_WEATHER                   //  194 个 (~ 6596 bytes) - 天气符号
// #define EMOTE_INCLUDE_NATURE                    //   24 个 (~  816 bytes) - 自然现象
// #define EMOTE_INCLUDE_ANIMALS                   //   64 个 (~ 2176 bytes) - 动物
// #define EMOTE_INCLUDE_FOOD_DRINK                //   76 个 (~ 2584 bytes) - 食物和饮料
// #define EMOTE_INCLUDE_ACTIVITIES                //   93 个 (~ 3162 bytes) - 活动
// #define EMOTE_INCLUDE_SPORTS                    //   50 个 (~ 1700 bytes) - 运动
// #define EMOTE_INCLUDE_TRAVEL_PLACES             //  128 个 (~ 4352 bytes) - 旅行和地点
// #define EMOTE_INCLUDE_OBJECTS                   //   92 个 (~ 3128 bytes) - 物品
// #define EMOTE_INCLUDE_SYMBOLS                   //  184 个 (~ 6256 bytes) - 装饰符号
// #define EMOTE_INCLUDE_ARROWS                    //  104 个 (~ 3536 bytes) - 箭头
// #define EMOTE_INCLUDE_GEOMETRIC_SHAPES          //   96 个 (~ 3264 bytes) - 几何形状
// #define EMOTE_INCLUDE_MATH_SYMBOLS              //  256 个 (~ 8704 bytes) - 数学符号
// #define EMOTE_INCLUDE_TECHNICAL                 //  256 个 (~ 8704 bytes) - 技术符号
// #define EMOTE_INCLUDE_CURRENCY                  //   48 个 (~ 1632 bytes) - 货币符号
// #define EMOTE_INCLUDE_NUMBER_FORMS              //   64 个 (~ 2176 bytes) - 数字形式
// #define EMOTE_INCLUDE_SUPPLEMENTAL_SYMBOLS      //  239 个 (~ 8126 bytes) - 补充符号
// #define EMOTE_INCLUDE_PLAYING_CARDS             //   96 个 (~ 3264 bytes) - 扑克牌
// #define EMOTE_INCLUDE_GAME_SYMBOLS              //  100 个 (~ 3400 bytes) - 游戏符号
// #define EMOTE_INCLUDE_ALPHANUMERIC              //  256 个 (~ 8704 bytes) - 字母数字
// #define EMOTE_INCLUDE_ENCLOSED_CJK              //  256 个 (~ 8704 bytes) - 带圈CJK
// #define EMOTE_INCLUDE_FULLWIDTH                 //  240 个 (~ 8160 bytes) - 全角字符

//
// 预设配置 (从小到大):
//
// #define EMOTE_TINY       // 仅基础集 (默认)
// #define EMOTE_MINIMAL    // 基础 + 常用表情 (SMILEYS_EMOTION + HAND_FINGERS)
// #define EMOTE_COMPACT    // MINIMAL + 天气自然 (WEATHER + NATURE)
// #define EMOTE_STANDARD   // COMPACT + 动物食物 (ANIMALS + FOOD_DRINK)
// #define EMOTE_EXTENDED   // STANDARD + 活动旅行 (ACTIVITIES + SPORTS + TRAVEL_PLACES)
// #define EMOTE_LARGE      // EXTENDED + 物品符号 (OBJECTS + SYMBOLS + ARROWS)
// #define EMOTE_FULL       // 所有分类

// ============================================================================
// 预设宏展开
// ============================================================================

// EMOTE_TINY: 默认配置，仅基础集
#ifdef EMOTE_TINY
  // 不定义任何扩展分类
#endif

// EMOTE_MINIMAL: 基础 + 常用表情
#ifdef EMOTE_MINIMAL
  #define EMOTE_INCLUDE_SMILEYS_EMOTION
  #define EMOTE_INCLUDE_HAND_FINGERS
#endif

// EMOTE_COMPACT: MINIMAL + 天气自然
#ifdef EMOTE_COMPACT
  #define EMOTE_INCLUDE_SMILEYS_EMOTION
  #define EMOTE_INCLUDE_HAND_FINGERS
  #define EMOTE_INCLUDE_WEATHER
  #define EMOTE_INCLUDE_NATURE
#endif

// EMOTE_STANDARD: COMPACT + 动物食物
#ifdef EMOTE_STANDARD
  #define EMOTE_INCLUDE_SMILEYS_EMOTION
  #define EMOTE_INCLUDE_HAND_FINGERS
  #define EMOTE_INCLUDE_WEATHER
  #define EMOTE_INCLUDE_NATURE
  #define EMOTE_INCLUDE_ANIMALS
  #define EMOTE_INCLUDE_FOOD_DRINK
#endif

// EMOTE_EXTENDED: STANDARD + 活动旅行
#ifdef EMOTE_EXTENDED
  #define EMOTE_INCLUDE_SMILEYS_EMOTION
  #define EMOTE_INCLUDE_HAND_FINGERS
  #define EMOTE_INCLUDE_WEATHER
  #define EMOTE_INCLUDE_NATURE
  #define EMOTE_INCLUDE_ANIMALS
  #define EMOTE_INCLUDE_FOOD_DRINK
  #define EMOTE_INCLUDE_ACTIVITIES
  #define EMOTE_INCLUDE_SPORTS
  #define EMOTE_INCLUDE_TRAVEL_PLACES
#endif

// EMOTE_LARGE: EXTENDED + 物品符号
#ifdef EMOTE_LARGE
  #define EMOTE_INCLUDE_SMILEYS_EMOTION
  #define EMOTE_INCLUDE_HAND_FINGERS
  #define EMOTE_INCLUDE_WEATHER
  #define EMOTE_INCLUDE_NATURE
  #define EMOTE_INCLUDE_ANIMALS
  #define EMOTE_INCLUDE_FOOD_DRINK
  #define EMOTE_INCLUDE_ACTIVITIES
  #define EMOTE_INCLUDE_SPORTS
  #define EMOTE_INCLUDE_TRAVEL_PLACES
  #define EMOTE_INCLUDE_OBJECTS
  #define EMOTE_INCLUDE_SYMBOLS
  #define EMOTE_INCLUDE_ARROWS
#endif

// EMOTE_FULL: 所有分类
#ifdef EMOTE_FULL
  #define EMOTE_INCLUDE_SMILEYS_EMOTION
  #define EMOTE_INCLUDE_HAND_FINGERS
  #define EMOTE_INCLUDE_WEATHER
  #define EMOTE_INCLUDE_NATURE
  #define EMOTE_INCLUDE_ANIMALS
  #define EMOTE_INCLUDE_FOOD_DRINK
  #define EMOTE_INCLUDE_ACTIVITIES
  #define EMOTE_INCLUDE_SPORTS
  #define EMOTE_INCLUDE_TRAVEL_PLACES
  #define EMOTE_INCLUDE_OBJECTS
  #define EMOTE_INCLUDE_SYMBOLS
  #define EMOTE_INCLUDE_ARROWS
  #define EMOTE_INCLUDE_GEOMETRIC_SHAPES
  #define EMOTE_INCLUDE_MATH_SYMBOLS
  #define EMOTE_INCLUDE_TECHNICAL
  #define EMOTE_INCLUDE_CURRENCY
  #define EMOTE_INCLUDE_NUMBER_FORMS
  #define EMOTE_INCLUDE_SUPPLEMENTAL_SYMBOLS
  #define EMOTE_INCLUDE_PLAYING_CARDS
  #define EMOTE_INCLUDE_GAME_SYMBOLS
  #define EMOTE_INCLUDE_MUSIC
  #define EMOTE_INCLUDE_ALPHANUMERIC
  #define EMOTE_INCLUDE_ENCLOSED_CJK
  #define EMOTE_INCLUDE_FULLWIDTH
#endif

// ============================================================================
// Emote Font 数据结构
// ============================================================================

typedef struct {
    const uint16_t* map;      // 压缩的 Unicode 码点映射表 (已排序)
    const uint8_t* data;      // 位图数据 (所有 16x16 bitmaps 连接)
    uint16_t count;           // Emote 数量
    uint8_t w;                // Emote 宽度 (固定 16)
    uint8_t h;                // Emote 高度 (固定 16)
} EmoteFont;

// ============================================================================
// Unicode 压缩/解压函数
// ============================================================================

inline uint16_t compressCodePoint(uint32_t codePoint) {
    if (codePoint >= 0x2000 && codePoint < 0x4000) {
        return (uint16_t)(codePoint - 0x2000);
    } else if (codePoint >= 0x1F000 && codePoint < 0x20000) {
        return (uint16_t)(0x4000 | (codePoint - 0x1F000));
    } else if (codePoint >= 0xE000 && codePoint < 0x10000) {
        return (uint16_t)(0x8000 | (codePoint - 0xE000));
    }
    return 0xFFFF;
}

inline uint32_t decompressCodePoint(uint16_t compressed) {
    uint8_t range = (compressed >> 14) & 0x03;
    uint16_t offset = compressed & 0x3FFF;
    
    switch (range) {
        case 0: return 0x2000 + offset;
        case 1: return 0x1F000 + offset;
        case 2: return 0xE000 + offset;
        default: return 0;
    }
}

// ============================================================================
// 导出的数据
// ============================================================================

extern const EmoteFont emoteFont;

// ============================================================================
// 查找和渲染函数
// ============================================================================

// 从 UTF-8 字符串解析 Unicode 码点，返回码点和消耗的字节数
inline uint32_t parseUtf8CodePoint(const char* str, size_t* bytesConsumed) {
    uint8_t c = (uint8_t)str[0];
    uint32_t codePoint = 0;
    size_t len = 1;

    if ((c & 0x80) == 0) {
        codePoint = c;
        len = 1;
    } else if ((c & 0xE0) == 0xC0) {
        codePoint = (c & 0x1F) << 6;
        codePoint |= ((uint8_t)str[1] & 0x3F);
        len = 2;
    } else if ((c & 0xF0) == 0xE0) {
        codePoint = (c & 0x0F) << 12;
        codePoint |= ((uint8_t)str[1] & 0x3F) << 6;
        codePoint |= ((uint8_t)str[2] & 0x3F);
        len = 3;
    } else if ((c & 0xF8) == 0xF0) {
        codePoint = (c & 0x07) << 18;
        codePoint |= ((uint8_t)str[1] & 0x3F) << 12;
        codePoint |= ((uint8_t)str[2] & 0x3F) << 6;
        codePoint |= ((uint8_t)str[3] & 0x3F);
        len = 4;
    }

    if (bytesConsumed) *bytesConsumed = len;
    return codePoint;
}

// 查找函数: 返回 emote 索引，-1 表示未找到 (使用二分查找)
int findEmoteIndex(uint32_t codePoint);

// 获取指定索引的位图数据指针
inline const uint8_t* getEmoteBitmap(int index) {
    if (index < 0 || index >= emoteFont.count) return nullptr;
    return emoteFont.data + (index * 32);  // 每个 16x16 位图 = 32 字节
}

// 检查字符串位置是否是 emote，返回 emote 索引和消耗的字节数
// 会跳过变体选择器 (U+FE0E, U+FE0F)
int matchEmoteAt(const char* str, size_t* bytesConsumed);

} // namespace graphics
