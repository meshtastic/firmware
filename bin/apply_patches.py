from os.path import join, isfile

Import("env")

LIBRARY_DIR = join (env["PROJECT_LIBDEPS_DIR"], env["PIOENV"], "RadioLib")
patchflag_path = join(LIBRARY_DIR, ".patching-done")
patch = join(env["PROJECT_DIR"], "bin", "patch_ng.py")

# patch file only if we didn't do it before
if not isfile(join(LIBRARY_DIR, ".patching-done")):
    original_path = join(LIBRARY_DIR)
    patch_file = join(env["PROJECT_DIR"], "patches", "0001-RadioLib-SPItransfer-virtual.patch")

    assert isfile(patch_file)

    env.Execute(
        env.VerboseAction(
            "$PYTHONEXE %s -p 1 --directory=%s %s" % (patch, original_path, patch_file)
           ,  "Applying patch to RadioLib"
        )
    )

    def _touch(path):
        with open(path, "w") as fp:
            fp.write("")

    env.Execute(lambda *args, **kwargs: _touch(patchflag_path))