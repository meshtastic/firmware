# RockBase IoT NM-EPD-420 — Button Operation

Board: ESP32-S3 + 4.2" 400×300 EPD (GDEY042Z98) + SX1262 LoRa + AHT20.
Two physical buttons: **BOOT** (GPIO0) and **USER** (GPIO45).

Button behavior depends on which firmware flavor is flashed.

## Classic E-Ink UI (`nm-epd-420`)

Standard Meshtastic 2.5+ base-UI two-button scheme: BOOT moves forward / confirms,
USER moves back / cancels.

| Button | Action | Behavior |
| ------ | ------ | -------- |
| BOOT | Short press | Next screen frame (Home → node list → messages → clock → system …) |
| BOOT | Long press (≥ 0.5 s) | Open the menu for the current frame (Home = main menu, system frame = system menu, message frame = reply / canned-message menu, …). Inside menus: short press = move highlight, long press = confirm selection |
| BOOT | Extra-long press (~5 s) | Shutdown flow (on-screen power-off confirmation) |
| USER | Short press | Previous screen frame (opposite direction of BOOT short press) |
| USER | Long press (≥ 0.5 s) | Back / cancel — exits menus, notification popups, and reply pickers one level up |

`BUTTON_CLICK_MS=200` sets the click/debounce timing.

## InkHUD UI (`nm-epd-420-inkhud`)

Wiring defined in `nicheGraphics.h` (`Inputs::TwoButton`):

| Button | Action | Behavior |
| ------ | ------ | -------- |
| BOOT | Short press | Next applet (All Messages → DMs → channels → map …) |
| BOOT | Long press | Open the InkHUD menu (rotation, tile layout, applet toggles, …). Inside menus: short press = move, long press = confirm |
| USER | Short press | `nextTile()` — cycles which applet occupies the second tile slot (Positions / Heard / …) |

Note: the USER-button confirmation chirp (`playChirp()`) targets a buzzer; this board has no
buzzer wired and the ES8311 speaker is not driven by Meshtastic audio code, so the chirp is
silent on NM-EPD-420.

---

# 按键操作说明（中文）

板上两颗按键：**BOOT**（GPIO0）与 **USER**（GPIO45）。行为取决于所刷固件环境。

## 经典 E-Ink UI（`nm-epd-420`）

Meshtastic 2.5+ 基础 UI 标准双键交互：BOOT 负责"前进 / 确认"，USER 负责"后退 / 取消"。

| 按键 | 操作 | 行为 |
| ---- | ---- | ---- |
| BOOT | 短按 | 切换到下一个屏幕页面（Home → 节点列表 → 消息 → 时钟 → 系统…循环） |
| BOOT | 长按（≥0.5 s） | 进入当前页面的菜单（Home=主菜单、系统页=系统菜单、消息页=回复/预设消息菜单…）；菜单内：短按=移动高亮，长按=确认 |
| BOOT | 超长按（约 5 s） | 关机流程（屏幕弹出关机确认） |
| USER | 短按 | 切换到上一个屏幕页面 |
| USER | 长按（≥0.5 s） | 返回 / 取消——在菜单、通知弹窗、回复选择框中退出到上一层 |

`BUTTON_CLICK_MS=200` 为单击/消抖判定时间。

## InkHUD UI（`nm-epd-420-inkhud`）

| 按键 | 操作 | 行为 |
| ---- | ---- | ---- |
| BOOT | 短按 | 切换到下一个 applet（消息 → DM → 频道 → 地图…循环） |
| BOOT | 长按 | 打开 InkHUD 菜单（屏幕旋转、tile 布局、applet 开关…）；菜单内短按移动、长按确认 |
| USER | 短按 | `nextTile()`——循环切换第二个 tile 槽位显示的 applet（Positions / Heard / …） |

注：USER 键的提示音走蜂鸣器接口（`playChirp()`），本板无蜂鸣器、扬声器未被驱动，实际无声。
