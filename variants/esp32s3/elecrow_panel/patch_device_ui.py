Import("env")

from pathlib import Path


libdeps = Path(env.subst("$PROJECT_LIBDEPS_DIR"))
pioenv = env.subst("$PIOENV")
source = (
    libdeps
    / pioenv
    / "meshtastic-device-ui"
    / "source"
    / "graphics"
    / "TFT"
    / "TFTView_320x240.cpp"
)

marker = "// ELECROW_NETWORK_ONLY_MAPS"
original = """    if (!map) {
#if LV_USE_FS_ARDUINO_SD
        map = new MapPanel(objects.raw_map_panel);
"""
replacement = """    if (!map) {
#if defined(HAS_ELECROW_STC)
        // ELECROW_NETWORK_ONLY_MAPS
        // Large CrowPanels fetch map tiles directly over Wi-Fi. The SD card
        // remains enabled for non-map storage, but is not queried for tiles.
        if (TileProvider::selectedTemplate() < 0) {
            int provider = TileProvider::addTemplate(
                \"Google Maps\", \"https://mt1.google.com/vt/lyrs=m&x={x}&y={y}&z={z}\");
            TileProvider::selectTemplate(provider);
        }
        map = new MapPanel(objects.raw_map_panel, new AsyncTileService(new URLService()));
#elif LV_USE_FS_ARDUINO_SD
        map = new MapPanel(objects.raw_map_panel);
"""

if not source.exists():
    raise RuntimeError(f"Meshtastic device UI source was not found: {source}")

text = source.read_text(encoding="utf-8")
if marker not in text:
    if original not in text:
        raise RuntimeError("The device UI map setup changed; Elecrow map patch was not applied")
    source.write_text(text.replace(original, replacement, 1), encoding="utf-8", newline="\n")
    print("Applied Elecrow network-only Google Maps patch")
