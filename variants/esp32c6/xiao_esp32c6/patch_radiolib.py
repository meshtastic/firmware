"""Patch RadioLib after PlatformIO downloads it"""
Import("env")
import os

def patch_radiolib(source, target, env):
    """Patch SX126x.cpp to skip setDio2AsRfSwitch on ESP32-C6"""
    radiolib_dir = env.subst("$PROJECT_LIBDEPS_DIR/$PIOENV/RadioLib")
    sx_file = os.path.join(radiolib_dir, "src", "modules", "SX126x", "SX126x.cpp")

    if not os.path.exists(sx_file):
        print(f"WARN: RadioLib SX126x.cpp not found at {sx_file}")
        return

    with open(sx_file, 'r') as f:
        content = f.read()

    # Patch 1: findChip() always returns true
    old_findchip = '''bool SX126x::findChip(const char* verStr) {
  uint8_t i = 0;
  bool flagFound = false;
  while((i < 10) && !flagFound) {'''
    if old_findchip in content:
        new_findchip = '''bool SX126x::findChip(const char* verStr) {
  (void)verStr; reset(true); return true; // PATCHED ESP32-C6
  uint8_t i = 0;
  bool flagFound = false;
  while((i < 10) && !flagFound) {'''  # dead code, never reached
        content = content.replace(old_findchip, new_findchip)
        print("PATCH: findChip() bypassed")
    else:
        print("WARN: findChip() patch target not found (may already be patched)")

    # Patch 2: setDio2AsRfSwitch(true) skipped
    old_dio2 = '''  state = setCurrentLimit(60.0);
  RADIOLIB_ASSERT(state);

  state = setDio2AsRfSwitch(true);
  RADIOLIB_ASSERT(state);

  state = setCRC(2);'''
    if old_dio2 in content:
        new_dio2 = '''  state = setCurrentLimit(60.0);
  RADIOLIB_ASSERT(state);

#ifndef SKIP_DIO2_RF_SWITCH
  state = setDio2AsRfSwitch(true);
  RADIOLIB_ASSERT(state);
#endif

  state = setCRC(2);'''
        content = content.replace(old_dio2, new_dio2)
        print("PATCH: setDio2AsRfSwitch() skipped")
    else:
        print("WARN: setDio2AsRfSwitch() patch target not found (may already be patched)")

    with open(sx_file, 'w') as f:
        f.write(content)

env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", patch_radiolib)
# Also run after lib download
env.AddPreAction("buildprog", patch_radiolib)
