#pragma once

// Simple compile-time UI string switch.
// Keep this lightweight to avoid runtime overhead on embedded targets.
#ifdef UI_LANG_ZH_CN
#define UI_STR(en, zh) (zh)
#else
#define UI_STR(en, zh) (en)
#endif

