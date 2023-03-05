# Copyright (c) 2014-present PlatformIO <contact@platformio.org>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import re
import subprocess
import sys

from platformio.project.exception import PlatformioException
from platformio.public import (
    DeviceMonitorFilterBase,
    load_build_metadata,
)

# By design, __init__ is called inside miniterm and we can't pass context to it.
# pylint: disable=attribute-defined-outside-init

IS_WINDOWS = sys.platform.startswith("win")


class Esp32C3ExceptionDecoder(DeviceMonitorFilterBase):
    NAME = "esp32_c3_exception_decoder"

    PCADDR_PATTERN = re.compile(r'0x4[0-9a-f]{7}', re.IGNORECASE)

    def __call__(self):
        self.buffer = ""
        self.pcaddr_re = self.PCADDR_PATTERN

        self.firmware_path = None
        self.addr2line_path = None
        self.enabled = self.setup_paths()

        if self.config.get("env:" + self.environment, "build_type") != "debug":
            print(
                """
Please build project in debug configuration to get more details about an exception.
See https://docs.platformio.org/page/projectconf/build_configurations.html

"""
            )

        return self

    def setup_paths(self):
        self.project_dir = os.path.abspath(self.project_dir)
        try:
            data = load_build_metadata(self.project_dir, self.environment)
            self.firmware_path = data["prog_path"]
            if not os.path.isfile(self.firmware_path):
                sys.stderr.write(
                    "%s: disabling, firmware at %s does not exist, rebuild the project?\n"
                    % (self.__class__.__name__, self.firmware_path)
                )
                return False

            if self.addr2line_path is None:
                cc_path = data.get("cc_path", "")
                if "-gcc" in cc_path:
                    self.addr2line_path = cc_path.replace("-gcc", "-addr2line")
                else:
                    sys.stderr.write(
                        "%s: disabling, failed to find addr2line.\n"
                        % self.__class__.__name__
                    )
                    return False
                
            if not os.path.isfile(self.addr2line_path):
                sys.stderr.write(
                    "%s: disabling, addr2line at %s does not exist\n"
                    % (self.__class__.__name__, self.addr2line_path)
                )
                return False
                
            return True
        except PlatformioException as e:
            sys.stderr.write(
                "%s: disabling, exception while looking for addr2line: %s\n"
                % (self.__class__.__name__, e)
            )
            return False

    def rx(self, text):
        if not self.enabled:
            return text

        last = 0
        while True:
            idx = text.find("\n", last)
            if idx == -1:
                if len(self.buffer) < 4096:
                    self.buffer += text[last:]
                break

            line = text[last:idx]
            if self.buffer:
                line = self.buffer + line
                self.buffer = ""
            last = idx + 1

            # Output each trace on a separate line below ours
            # Logic identical to https://github.com/espressif/esp-idf/blob/master/tools/idf_monitor_base/logger.py#L131
            for m in re.finditer(self.pcaddr_re, line):
                if m is None:
                    continue

                trace = self.get_backtrace(m)
                if len(trace) != "":
                    text = text[: last] + trace + text[last :]
                    last += len(trace)

        return text

    def get_backtrace(self, match):
        trace = "\n"
        enc = "mbcs" if IS_WINDOWS else "utf-8"
        args = [self.addr2line_path, u"-fipC", u"-e", self.firmware_path]
        try:
            addr = match.group()
            output = (
                subprocess.check_output(args + [addr])
                .decode(enc)
                .strip()
            )
            output = output.replace(
                "\n", "\n     "
            )  # newlines happen with inlined methods
            output = self.strip_project_dir(output)
            # Output the trace in yellow color so that it is easier to spot
            trace += "\033[33m=> %s: %s\033[0m\n" % (addr, output)
        except subprocess.CalledProcessError as e:
            sys.stderr.write(
                "%s: failed to call %s: %s\n"
                % (self.__class__.__name__, self.addr2line_path, e)
            )
        return trace

    def strip_project_dir(self, trace):
        while True:
            idx = trace.find(self.project_dir)
            if idx == -1:
                break
            trace = trace[:idx] + trace[idx + len(self.project_dir) + 1 :]
        return trace
