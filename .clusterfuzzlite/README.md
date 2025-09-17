# ClusterFuzzLite for Meshtastic

This directory contains the fuzzer implementation for Meshtastic using the ClusterFuzzLite framework.
See the [ClusterFuzzLite documentation](https://google.github.io/clusterfuzzlite/) for more details.

## Running locally

ClusterFuzzLite uses the OSS-Fuzz toolchain. To build the fuzzer manually, first grab a copy of OSS-Fuzz.

```shell
git clone https://github.com/google/oss-fuzz.git
cd oss-fuzz
```

To build the fuzzer, run:

```shell
python3 infra/helper.py build_image --external $PATH_TO_MESHTASTIC_FIRMWARE_DIRECTORY
python3 infra/helper.py build_fuzzers --external $PATH_TO_MESHTASTIC_FIRMWARE_DIRECTORY --sanitizer address
```

To run the fuzzer, run:

```shell
python3 infra/helper.py run_fuzzer --external --corpus-dir=<path-to-temp-corpus-dir> $PATH_TO_MESHTASTIC_FIRMWARE_DIRECTORY router_fuzzer
```

More background on these commands can be found in the
[ClusterFuzzLite documentation](https://google.github.io/clusterfuzzlite/build-integration/#testing-locally).

## router_fuzzer.cpp

This fuzzer submits MeshPacket protos to the `Router::enqueueReceivedMessage` method. It takes the binary
data from the fuzzer and decodes that data to a MeshPacket using nanopb. A few fields in
the MeshPacket are modified by the fuzzer.

- If the `to` field is 0, it will be replaced with the NodeID of the running node.
- If the `from` field is 0, it will be replaced with the NodeID of the running node.
- If the `id` field is 0, it will be replaced with an incrementing counter value.
- If the `pki_encrypted` field is true, the `public_key` field will be populated with the first admin key.

The `router_fuzzer_seed_corpus.py` file contains a list of MeshPackets. It is run from inside build.sh and
writes the binary MeshPacket protos to files. These files are use used by the fuzzer as its initial seed data,
helping the fuzzer to start off with a few known inputs.

### Interpreting a fuzzer crash

If the fuzzer crashes, it'll write the input bytes used for the test case to a file and notify about the
location of that file. The contents of the file are a binary serialized MeshPacket protobuf. The following
snippet of Python code can be used to parse the file into a human readable form.

```python
from meshtastic.protobuf import mesh_pb2

mesh_pb2.MeshPacket.FromString(open("crash-XXXX-file", "rb").read())
```

Consider adding any such crash results to the `router_fuzzer_seed_corpus.py` file to ensure there a isn't
a future regression for that crash test case.
