#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
#
# Archive nrf52 libraries with the LTO-plugin-aware binutils wrappers.
#
# -flto (variants/nrf52840/nrf52.ini) emits slim LTO objects: no machine code and no regular
# symbol table, only .gnu.lto_* sections. Plain `ar`/`ranlib` can index those only if binutils
# auto-loads liblto_plugin from <toolchain>/lib/bfd-plugins/. The GCC 12 package ships that
# symlink; the GCC 14 package does not, so every lib*.a gets an archive index with no LTO
# symbols and the link dies with hundreds of "undefined reference" errors (the linker only
# pulls archive members the index advertises). `gcc-ar`/`gcc-ranlib` pass --plugin explicitly,
# so the index is right whatever the toolchain package ships.
#
# Must be a POST script: the platform builder does env.Replace(AR=..., RANLIB=...) after pre:
# scripts run, and each library is built with its own env cloned before post scripts run --
# hence the walk over the lib builders instead of a single Replace on the global env.
#
# TEMPORARY: this is a local stand-in for the upstream fix,
#   https://github.com/platformio/platform-nordicnrf52/pull/215
# which makes the platform builder itself select gcc-ar/gcc-ranlib (as platform-ststm32 already
# does). Once the platformio/nordicnrf52 release pinned in variants/nrf52840/nrf52.ini includes
# that change, delete this script and its extra_scripts entry there. Until then the two are
# safe to run together: this only re-sets the same values.
Import("env", "projenv")

_envs = [env, projenv] + [lb.env for lb in env.GetLibBuilders()]
for _e in _envs:
    _e.Replace(AR="arm-none-eabi-gcc-ar", RANLIB="arm-none-eabi-gcc-ranlib")
