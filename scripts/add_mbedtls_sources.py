# Pulls all mbedTLS sources from arduino-pico's pico-sdk into the build so the
# firmware can call mbedtls_* APIs (the precompiled arduino-pico libs only
# expose BearSSL; mbedTLS is shipped as source under lib/mbedtls/library/*.c).
#
# Wired in via `extra_scripts = pre:scripts/add_mbedtls_sources.py` on the
# variants that define HAS_ETHERNET_TLS_API. POSIX-only code paths inside
# mbedtls are neutralized by src/mbedtls_user_config.h (referenced via the
# variant's -DMBEDTLS_USER_CONFIG_FILE build flag). Unused symbols are dropped
# at link time, so non-TLS envs that pull this script in pay nothing.

# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): Import/env/Return are SCons-injected globals
# trunk-ignore-all(ruff/E402)
# trunk-ignore-all(flake8/E402): stdlib imports must follow Import("env")
Import("env")

import glob
import os

framework_dir = env.PioPlatform().get_package_dir("framework-arduinopico")
if not framework_dir:
    print("[add_mbedtls_sources] framework-arduinopico package not found - skipping")
    Return()

mbedtls_root = os.path.join(framework_dir, "pico-sdk", "lib", "mbedtls")
include_dir = os.path.join(mbedtls_root, "include")
src_dir = os.path.join(mbedtls_root, "library")

if not os.path.isdir(src_dir):
    print(f"[add_mbedtls_sources] mbedtls library dir not found at {src_dir}")
    Return()

# mbedtls headers + project src (where mbedtls_user_config.h lives) must be on
# the include path when the .c files compile.
env.Append(CPPPATH=[include_dir, env["PROJECT_SRC_DIR"]])

# Inject the user-config define through CPPDEFINES so SCons handles the
# embedded quotes correctly. The build_flags shell parser drops/corrupts the
# value when the same is expressed as -D 'MBEDTLS_USER_CONFIG_FILE="..."'.
env.Append(CPPDEFINES=[("MBEDTLS_USER_CONFIG_FILE", '\\"mbedtls_user_config.h\\"')])

sources = sorted(glob.glob(os.path.join(src_dir, "*.c")))
print(
    f"[add_mbedtls_sources] Adding {len(sources)} mbedTLS source files from {src_dir}"
)

env.BuildSources(
    os.path.join("$BUILD_DIR", "mbedtls_pico"),
    src_dir,
    src_filter=["+<*.c>"],
)
