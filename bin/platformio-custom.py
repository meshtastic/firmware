import configparser

config = configparser.RawConfigParser()
config.read('version.properties')

version = dict(config.items('VERSION'))

verStr = "{}.{}.{}".format(version["major"], version["minor"], version["build"])

print(f"Using meshtastic platform-custom.py, firmare version {verStr}")

Import("env", "projenv")

# General options that are passed to the C and C++ compilers
projenv.Append(CCFLAGS=[
    f"-DAPP_VERSION={verStr}"
    ])