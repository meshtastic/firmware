from readprops import readProps
Import("env")

verObj = readProps("./version.properties")

print("Using meshtastic version_script.py, firmware version " + verObj["long"] + " on " + env.get("PIOENV"))

env.Append(BUILD_FLAGS=[
    "-DAPP_VERSION=" + verObj["long"],
    "-DAPP_VERSION_SHORT=" + verObj["short"],
    "-DAPP_ENV=" + env.get("PIOENV"),
])