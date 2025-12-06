#!/usr/bin/env python3

"""ESP Exception Decoder

github:  https://github.com/janLo/EspArduinoExceptionDecoder
license: GPL v3
author:  Jan Losinski

Meshtastic notes:
* original version is at: https://github.com/janLo/EspArduinoExceptionDecoder
* version that's checked into meshtastic repo is based on: https://github.com/me21/EspArduinoExceptionDecoder
  which adds in ESP32 Backtrace decoding.
* this also updates the defaults to use ESP32, instead of ESP8266 and defaults to the built firmware.bin
* also updated the toolchain name, which will be set according to the platform

To use, copy the "Backtrace: 0x...." line to a file, e.g., backtrace.txt, then run:
$ bin/exception_decoder.py backtrace.txt
For a platform other than ESP32, use the -p option, e.g.:
$ bin/exception_decoder.py -p ESP32S3 backtrace.txt
To specify a specific .elf file, use the -e option, e.g.:
$ bin/exception_decoder.py -e firmware.elf backtrace.txt
"""

import argparse
import os
import re
import subprocess
import sys
from collections import namedtuple

EXCEPTIONS = [
    "Illegal instruction",
    "SYSCALL instruction",
    "InstructionFetchError: Processor internal physical address or data error during instruction fetch",
    "LoadStoreError: Processor internal physical address or data error during load or store",
    "Level1Interrupt: Level-1 interrupt as indicated by set level-1 bits in the INTERRUPT register",
    "Alloca: MOVSP instruction, if caller's registers are not in the register file",
    "IntegerDivideByZero: QUOS, QUOU, REMS, or REMU divisor operand is zero",
    "reserved",
    "Privileged: Attempt to execute a privileged operation when CRING ? 0",
    "LoadStoreAlignmentCause: Load or store to an unaligned address",
    "reserved",
    "reserved",
    "InstrPIFDataError: PIF data error during instruction fetch",
    "LoadStorePIFDataError: Synchronous PIF data error during LoadStore access",
    "InstrPIFAddrError: PIF address error during instruction fetch",
    "LoadStorePIFAddrError: Synchronous PIF address error during LoadStore access",
    "InstTLBMiss: Error during Instruction TLB refill",
    "InstTLBMultiHit: Multiple instruction TLB entries matched",
    "InstFetchPrivilege: An instruction fetch referenced a virtual address at a ring level less than CRING",
    "reserved",
    "InstFetchProhibited: An instruction fetch referenced a page mapped with an attribute that does not permit instruction fetch",
    "reserved",
    "reserved",
    "reserved",
    "LoadStoreTLBMiss: Error during TLB refill for a load or store",
    "LoadStoreTLBMultiHit: Multiple TLB entries matched for a load or store",
    "LoadStorePrivilege: A load or store referenced a virtual address at a ring level less than CRING",
    "reserved",
    "LoadProhibited: A load referenced a page mapped with an attribute that does not permit loads",
    "StoreProhibited: A store referenced a page mapped with an attribute that does not permit stores",
]

PLATFORMS = {
    "ESP8266": "xtensa-lx106",
    "ESP32": "xtensa-esp32",
    "ESP32S3": "xtensa-esp32s3",
    "ESP32C3": "riscv32-esp",
}
TOOLS = {
    "ESP8266": "xtensa",
    "ESP32": "xtensa-esp32",
    "ESP32S3": "xtensa-esp32s3",
    "ESP32C3": "riscv32-esp",
}

BACKTRACE_REGEX = re.compile(
    r"(?:\s+(0x40[0-2](?:\d|[a-f]|[A-F]){5}):0x(?:\d|[a-f]|[A-F]){8})\b"
)
EXCEPTION_REGEX = re.compile("^Exception \\((?P<exc>[0-9]*)\\):$")
COUNTER_REGEX = re.compile(
    "^epc1=(?P<epc1>0x[0-9a-f]+) epc2=(?P<epc2>0x[0-9a-f]+) epc3=(?P<epc3>0x[0-9a-f]+) "
    "excvaddr=(?P<excvaddr>0x[0-9a-f]+) depc=(?P<depc>0x[0-9a-f]+)$"
)
CTX_REGEX = re.compile("^ctx: (?P<ctx>.+)$")
POINTER_REGEX = re.compile(
    "^sp: (?P<sp>[0-9a-f]+) end: (?P<end>[0-9a-f]+) offset: (?P<offset>[0-9a-f]+)$"
)
STACK_BEGIN = ">>>stack>>>"
STACK_END = "<<<stack<<<"
STACK_REGEX = re.compile(
    "^(?P<off>[0-9a-f]+):\W+(?P<c1>[0-9a-f]+) (?P<c2>[0-9a-f]+) (?P<c3>[0-9a-f]+) (?P<c4>[0-9a-f]+)(\W.*)?$"
)

StackLine = namedtuple("StackLine", ["offset", "content"])


class ExceptionDataParser(object):
    def __init__(self):
        self.exception = None

        self.epc1 = None
        self.epc2 = None
        self.epc3 = None
        self.excvaddr = None
        self.depc = None

        self.ctx = None

        self.sp = None
        self.end = None
        self.offset = None

        self.stack = []

    def _parse_backtrace(self, line):
        if line.startswith("Backtrace:"):
            self.stack = [
                StackLine(offset=0, content=(addr,))
                for addr in BACKTRACE_REGEX.findall(line)
            ]
            return None
        return self._parse_backtrace

    def _parse_exception(self, line):
        match = EXCEPTION_REGEX.match(line)
        if match is not None:
            self.exception = int(match.group("exc"))
            return self._parse_counters
        return self._parse_exception

    def _parse_counters(self, line):
        match = COUNTER_REGEX.match(line)
        if match is not None:
            self.epc1 = match.group("epc1")
            self.epc2 = match.group("epc2")
            self.epc3 = match.group("epc3")
            self.excvaddr = match.group("excvaddr")
            self.depc = match.group("depc")
            return self._parse_ctx
        return self._parse_counters

    def _parse_ctx(self, line):
        match = CTX_REGEX.match(line)
        if match is not None:
            self.ctx = match.group("ctx")
            return self._parse_pointers
        return self._parse_ctx

    def _parse_pointers(self, line):
        match = POINTER_REGEX.match(line)
        if match is not None:
            self.sp = match.group("sp")
            self.end = match.group("end")
            self.offset = match.group("offset")
            return self._parse_stack_begin
        return self._parse_pointers

    def _parse_stack_begin(self, line):
        if line == STACK_BEGIN:
            return self._parse_stack_line
        return self._parse_stack_begin

    def _parse_stack_line(self, line):
        if line != STACK_END:
            match = STACK_REGEX.match(line)
            if match is not None:
                self.stack.append(
                    StackLine(
                        offset=match.group("off"),
                        content=(
                            match.group("c1"),
                            match.group("c2"),
                            match.group("c3"),
                            match.group("c4"),
                        ),
                    )
                )
            return self._parse_stack_line
        return None

    def parse_file(self, file, platform, stack_only=False):
        if platform != "ESP8266":
            func = self._parse_backtrace
        else:
            func = self._parse_exception
            if stack_only:
                func = self._parse_stack_begin

        for line in file:
            func = func(line.strip())
            if func is None:
                break

        if func is not None:
            print("ERROR: Parser not complete!")
            sys.exit(1)


class AddressResolver(object):
    def __init__(self, tool_path, elf_path):
        self._tool = tool_path
        self._elf = elf_path
        self._address_map = {}

    def _lookup(self, addresses):
        cmd = [self._tool, "-aipfC", "-e", self._elf] + [
            addr for addr in addresses if addr is not None
        ]

        if sys.version_info[0] < 3:
            output = subprocess.check_output(cmd)
        else:
            output = subprocess.check_output(cmd, encoding="utf-8")

        line_regex = re.compile("^(?P<addr>[0-9a-fx]+): (?P<result>.+)$")

        last = None
        for line in output.splitlines():
            line = line.strip()
            match = line_regex.match(line)

            if match is None:
                if last is not None and line.startswith("(inlined by)"):
                    line = line[12:].strip()
                    self._address_map[last] += "\n  \-> inlined by: " + line
                continue

            if match.group("result") == "?? ??:0":
                continue

            self._address_map[match.group("addr")] = match.group("result")
            last = match.group("addr")

    def fill(self, parser):
        addresses = [
            parser.epc1,
            parser.epc2,
            parser.epc3,
            parser.excvaddr,
            parser.sp,
            parser.end,
            parser.offset,
        ]
        for line in parser.stack:
            addresses.extend(line.content)

        self._lookup(addresses)

    def _sanitize_addr(self, addr):
        if addr.startswith("0x"):
            addr = addr[2:]

        fill = "0" * (8 - len(addr))
        return "0x" + fill + addr

    def resolve_addr(self, addr):
        out = self._sanitize_addr(addr)

        if out in self._address_map:
            out += ": " + self._address_map[out]

        return out

    def resolve_stack_addr(self, addr, full=True):
        addr = self._sanitize_addr(addr)
        if addr in self._address_map:
            return addr + ": " + self._address_map[addr]

        if full:
            return "[DATA (0x" + addr + ")]"

        return None


def print_addr(name, value, resolver):
    print("{}:{} {}".format(name, " " * (8 - len(name)), resolver.resolve_addr(value)))


def print_stack_full(lines, resolver):
    print("stack:")
    for line in lines:
        print(str(line.offset) + ":")
        for content in line.content:
            print("  " + resolver.resolve_stack_addr(content))


def print_stack(lines, resolver):
    print("stack:")
    for line in lines:
        for content in line.content:
            out = resolver.resolve_stack_addr(content, full=False)
            if out is None:
                continue
            print(out)


def print_result(parser, resolver, platform, full=True, stack_only=False):
    if platform == "ESP8266" and not stack_only:
        print(
            "Exception: {} ({})".format(parser.exception, EXCEPTIONS[parser.exception])
        )

        print("")
        print_addr("epc1", parser.epc1, resolver)
        print_addr("epc2", parser.epc2, resolver)
        print_addr("epc3", parser.epc3, resolver)
        print_addr("excvaddr", parser.excvaddr, resolver)
        print_addr("depc", parser.depc, resolver)

        print("")
        print("ctx: " + parser.ctx)

        print("")
        print_addr("sp", parser.sp, resolver)
        print_addr("end", parser.end, resolver)
        print_addr("offset", parser.offset, resolver)

        print("")
    if full:
        print_stack_full(parser.stack, resolver)
    else:
        print_stack(parser.stack, resolver)


def parse_args():
    parser = argparse.ArgumentParser(description="decode ESP Stacktraces.")

    parser.add_argument(
        "-p",
        "--platform",
        help="The platform to decode from",
        choices=PLATFORMS.keys(),
        default="ESP32",
    )
    parser.add_argument(
        "-t",
        "--tool",
        help="Path to the toolchain (without specific platform)",
        default="~/.platformio/packages/toolchain-",
    )
    parser.add_argument(
        "-e", "--elf", help="path to elf file", default=".pio/build/tbeam/firmware.elf"
    )
    parser.add_argument(
        "-f", "--full", help="Print full stack dump", action="store_true"
    )
    parser.add_argument(
        "-s", "--stack_only", help="Decode only a stractrace", action="store_true"
    )
    parser.add_argument(
        "file",
        help="The file to read the exception data from ('-' for STDIN)",
        default="-",
    )

    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()

    if args.file == "-":
        file = sys.stdin
    else:
        if not os.path.exists(args.file):
            print("ERROR: file " + args.file + " not found")
            sys.exit(1)
        file = open(args.file, "r")

    addr2line = os.path.join(
        os.path.abspath(os.path.expanduser(args.tool + TOOLS[args.platform])),
        "bin/" + PLATFORMS[args.platform] + "-elf-addr2line",
    )
    if os.name == "nt":
        addr2line += ".exe"
    if not os.path.exists(addr2line):
        print("ERROR: addr2line not found (" + addr2line + ")")

    elf_file = os.path.abspath(os.path.expanduser(args.elf))
    if not os.path.exists(elf_file):
        print("ERROR: elf file not found (" + elf_file + ")")

    parser = ExceptionDataParser()
    resolver = AddressResolver(addr2line, elf_file)

    parser.parse_file(file, args.platform, args.stack_only)
    resolver.fill(parser)

    print_result(parser, resolver, args.platform, args.full, args.stack_only)
