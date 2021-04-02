# Running github actions locally

If you'd like to run the **same** integration tests we run on github but on your own machine, you can do the following.

1. Install homebrew per https://brew.sh/
2. Install https://github.com/nektos/act with "brew install act"
3. cd into meshtastic-device and run "act"
4. Select a "medium" sized image
5. Alas - this "act" build **almost** works, but fails because it can't find lib/nanopb/include/pb.h!  So someone (you the reader? @geeksville ays hopefully...) needs to fix that before these instructions are complete.
6. 