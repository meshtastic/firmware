# CJK + Japanese IME GitHub Actions build

This directory contains everything needed to build CJK + Japanese-IME firmware
for **Wio Tracker L1** and **gat562 Mesh Trial Tracker** in GitHub Actions.

## What gets built

The workflow at `.github/workflows/build-cjk.yml` runs on every push, every PR
to default branches, daily at 06:00 UTC, and on manual dispatch.

The build SOURCE is `ssp97/meshtastic_fw/develop`, which already has Chinese
pinyin IME and CJK display. On top of that, the workflow:

1. Applies `patches/0016-feat-add-Japanese-romaji-to-kana-IME.patch`
   — adds `JapaneseIME.{h,cpp}` and integrates with `VirtualKeyboard`.
2. Applies every `.patch` file in `extra-patches/`
   — **drop your spoof-id work here**.
3. Generates a CJK font (GB2312 + JP kana + KR Hangul) using Noto Sans CJK
   downloaded fresh on each build.
4. Overlays variant-specific build flags:
   - **Wio L1**: full replacement (stock had no exclusions).
   - **gat562**: injects `-DJP_IME=1` into existing `[env:gat562_mesh_base]`.
5. Builds both targets in parallel.
6. Reports flash usage vs the 815,104-byte ceiling (fails if over).
7. Uploads UF2 / HEX / ELF artifacts under
   `firmware-<target>-<run-number>` for 30 days.

## Flash budget — what fits

The nRF52840 app slot is **815,104 bytes (796 KB)**. Realistic accounting:

| component | size |
|---|---|
| stock Meshtastic firmware (minus our exclusions) | ~500 KB |
| `libpinyin_simple_backend.a` (cortex-m4f, with dictionary) | 67 KB |
| `emotes.cpp` (1,500 emoji bitmaps) | ~150 KB |
| `JapaneseIME.cpp/.h` | 10 KB |
| **subtotal before font** | **~727 KB** |
| **headroom for the CJK font** | **~88 KB** |

Font footprint table (binary data, not source size):

| font size | chars | binary size |
|---|---:|---:|
| 10×10, GB2312 | ~7,000 | ~136 KB |
| 10×10, GB2312 + Kana | ~7,250 | ~140 KB |
| 10×10, GB2312 + Kana + Hangul | ~18,000 | ~351 KB |
| 12×12, GB2312 + Kana | ~7,250 | ~165 KB |
| 12×12, GB2312 + Kana + Hangul | ~18,000 | ~421 KB |

**Default workflow config (size 10, GB2312 + Kana, NO Hangul):**
overshoots the 88 KB headroom by ~50 KB → you'll need to drop emoji to fit
(see "If a build fails over 815 KB" below). Or accept the tighter ~50 KB
overshoot if you also strip another feature.

**Hangul = ~+250 KB extra**. Will not fit unless you also drop emoji AND
trim more features. Off by default. Enable via the workflow_dispatch input
`enable_hangul = true` if you really want KR display.

## Workflow inputs (workflow_dispatch only)

When you trigger from the Actions tab → "Run workflow":

| input | default | meaning |
|---|---|---|
| `enable_hangul` | `false` | Include Korean Hangul Syllables in the font. ~+250 KB. |
| `font_size` | `10` | OLED font box size (px). Matches gat562's `OLED_CJK_SIZE=10`. Use 12 only if you've stripped enough other features to fit. |
| `apply_spoof_id` | `true` | Cherry-pick Tauooo's SETNODEID feature onto the build. Set false to test a clean ssp97 build. |

Push and schedule triggers always use the defaults.

## Adding your spoof-id functionality

**Already automated.** The workflow cherry-picks Tauooo's 4 SETNODEID commits
from `Tauooo/firmware` directly onto the build base on every run:

```
655751c69  Add files via upload          (NodeDB::setNodeNum)
0e61c9990  Implement node ID setting in AdminModule
fa0527883  Add files via upload          (CannedMessageModule UI)
e23cba11b  Rename HEX to hexChars for consistency
```

The cherry-pick uses `-X theirs` to auto-resolve any include-order or
whitespace conflicts that arise from upstream churn. The semantic SETNODEID
additions land in areas ssp97 doesn't modify, so this is safe — the
verification step at the end of the cherry-pick block prints how many
`SETNODEID` strings exist in the resulting tree (should be ≥10).

If those commit SHAs ever disappear from `Tauooo/firmware` (rebase, force-push,
deleted branch), the build will fail loudly at that step. Update the SHA list
in `.github/workflows/build-cjk.yml` → "Cherry-pick Tauooo SETNODEID …" step.

To **disable** the spoof-id cherry-pick for a single build (e.g. to test a
clean ssp97-only build), use the manual workflow_dispatch input
`apply_spoof_id = false`.

## Adding OTHER custom patches (beyond spoof-id)

## Targets

| env name | board | display | LoRa | flash budget | exclusions applied |
|---|---|---|---|---|---|
| `seeed_wio_tracker_L1` | Wio Tracker L1 | SSD1306 OLED | LR1110 | 815,104 B | SX127x, SX128x, RangeTest, DetectionSensor, Serial, ATAK, BBQ10/MPR121/TCA8418 KB |
| `gat562_mesh_trial_tracker_zhcn` | GAT562 | SSD1306 OLED | (per ssp97) | 815,104 B | (ssp97's existing list — same as above) |

Want a third target (e.g. another nRF52840 board)? Add it to:
- `.github/workflows/build-cjk.yml` → `matrix.target`
- Add a per-target case in `.github/cjk/scripts/apply_variant.sh`
- (If full overlay needed) a `<target>.platformio.ini` in `.github/cjk/variants/`

## Triggering a build

- **Push** to `develop`, `main`, or `master` → builds.
- **Open a PR** → builds for review.
- **Manual** → Actions tab → "Build CJK + Japanese IME firmware" → Run workflow.
- **Daily 06:00 UTC** → automatic, picks up whatever ssp97 pulled from upstream.

## Where the firmware ends up

Each run produces two artifact bundles:

- `firmware-seeed_wio_tracker_L1-<N>` — UF2 to drag-and-drop onto the Wio L1
- `firmware-gat562_mesh_trial_tracker_zhcn-<N>` — UF2 for gat562

Click the workflow run in the Actions tab → scroll to "Artifacts" → download.

## Local equivalent (if you want to test before pushing)

```bash
# clone ssp97 separately
git clone --depth 50 https://github.com/ssp97/meshtastic_fw base
cd base
git submodule update --init --recursive

# apply the JP IME patch
git config user.email "x@x" && git config user.name "x"
git am ../<this-repo>/.github/cjk/patches/0016-*.patch

# (optional) apply your extras
for p in ../<this-repo>/.github/cjk/extra-patches/*.patch; do git am "$p"; done

# generate font
pip install Pillow
curl -sSL -o noto.ttc https://github.com/notofonts/noto-cjk/raw/main/Sans/OTC/NotoSansCJK-Regular.ttc
python3 ../<this-repo>/.github/cjk/gen_oled_utf8_font.py \
    --size 12 --charset gb2312 --kana --hangul \
    --ttf noto.ttc --output src/graphics/fonts/utf8_12x12.h --prefix utf8_12x12_
bash ../<this-repo>/.github/cjk/scripts/stub_other_sizes.sh src/graphics/fonts 12

# overlay variant
bash ../<this-repo>/.github/cjk/scripts/apply_variant.sh \
    seeed_wio_tracker_L1 ../<this-repo>/.github/cjk/variants

# build
pio run -e seeed_wio_tracker_L1
```

## Testing the IME logic without flashing

```bash
g++ -std=c++17 -O2 \
    .github/cjk/test_japanese_ime.cpp \
    .github/cjk/JapaneseIME.cpp \
    -o /tmp/test_ime
/tmp/test_ime
# Expect: "Summary: 63 pass, 0 fail"
```

## Flash budget — what to do if a build fails over the 815 KB ceiling

The workflow fails the build with a clear message. Options in priority order:

1. **Drop emoji** — saves ~150–200 KB. In the variant overlay, exclude
   `<graphics/emotes.cpp>` from `build_src_filter` and add
   `-DMESHTASTIC_EXCLUDE_EMOTES=1` (you may need to add stub references
   in `MessageRenderer.cpp` if that macro isn't already wired in upstream).
2. **Smaller font** — switch `OLED_CJK_SIZE` from `12` to `10` (~70 KB
   savings) and regenerate the 10x10 instead of 12x12 in the workflow.
3. **Smaller charset** — keep `gb2312` (~6.7k chars). If you switched to
   `gbk`, that's why you're over.
4. **More exclusions** — add `MESHTASTIC_EXCLUDE_*` for features you don't
   use. Check the existing exclusion list in `nrf52_base.build_flags` first.
