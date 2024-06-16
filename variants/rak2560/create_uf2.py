import struct

Import("env")  # noqa: F821


# Parse input and create UF2 file
def create_uf2(source, target, env):
    # source_hex = target[0].get_abspath()
    source_hex = target[0].get_string(False)
    source_hex = ".\\" + source_hex
    print("#########################################################")
    print("Create UF2 from " + source_hex)
    print("#########################################################")
    # print("Source: " + source_hex)
    target = source_hex.replace(".hex", "")
    target = target + ".uf2"
    # print("Target: " + target)

    with open(source_hex, mode="rb") as f:
        inpbuf = f.read()

    outbuf = convert_from_hex_to_uf2(inpbuf.decode("utf-8"))

    write_file(target, outbuf)
    print("#########################################################")
    print(target + " is ready to flash to target device")
    print("#########################################################")


# Add callback after .hex file was created
env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", create_uf2)  # noqa: F821

# UF2 creation taken from uf2conv.py
UF2_MAGIC_START0 = 0x0A324655  # "UF2\n"
UF2_MAGIC_START1 = 0x9E5D5157  # Randomly selected
UF2_MAGIC_END = 0x0AB16F30  # Ditto

familyid = 0xADA52840


class Block:
    def __init__(self, addr):
        self.addr = addr
        self.bytes = bytearray(256)

    def encode(self, blockno, numblocks):
        global familyid
        flags = 0x0
        if familyid:
            flags |= 0x2000
        hd = struct.pack(
            "<IIIIIIII",
            UF2_MAGIC_START0,
            UF2_MAGIC_START1,
            flags,
            self.addr,
            256,
            blockno,
            numblocks,
            familyid,
        )
        hd += self.bytes[0:256]
        while len(hd) < 512 - 4:
            hd += b"\x00"
        hd += struct.pack("<I", UF2_MAGIC_END)
        return hd


def write_file(name, buf):
    with open(name, "wb") as f:
        f.write(buf)
    # print("Wrote %d bytes to %s." % (len(buf), name))


def convert_from_hex_to_uf2(buf):
    global appstartaddr
    appstartaddr = None
    upper = 0
    currblock = None
    blocks = []
    for line in buf.split("\n"):
        if line[0] != ":":
            continue
        i = 1
        rec = []
        while i < len(line) - 1:
            rec.append(int(line[i : i + 2], 16))
            i += 2
        tp = rec[3]
        if tp == 4:
            upper = ((rec[4] << 8) | rec[5]) << 16
        elif tp == 2:
            upper = ((rec[4] << 8) | rec[5]) << 4
            assert (upper & 0xFFFF) == 0
        elif tp == 1:
            break
        elif tp == 0:
            addr = upper | (rec[1] << 8) | rec[2]
            if appstartaddr is None:
                appstartaddr = addr
            i = 4
            while i < len(rec) - 1:
                if not currblock or currblock.addr & ~0xFF != addr & ~0xFF:
                    currblock = Block(addr & ~0xFF)
                    blocks.append(currblock)
                currblock.bytes[addr & 0xFF] = rec[i]
                addr += 1
                i += 1
    numblocks = len(blocks)
    resfile = b""
    for i in range(0, numblocks):
        resfile += blocks[i].encode(i, numblocks)
    return resfile
