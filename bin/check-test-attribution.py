#!/usr/bin/env python3
"""Verify each PlatformIO JUnit report ran the suite it claims to have run.

PlatformIO links every native test program to one path ($BUILD_DIR/$PROGNAME) and parses
Unity output textually, without checking that the reported source file belongs to the suite
it is running. Split a run into `--without-testing` then `--without-building` and every suite
executes whichever binary was linked last, all reporting PASSED. This reads the JUnit reports
that run already produces and fails on the two shapes that hides:

  MISATTRIBUTED - a test case whose source file lives outside the suite that reported it
  EMPTY         - a suite that was asked to run and produced no test cases at all

Usage:
  check-test-attribution.py [--expect "s1 s2"]... [--label TEXT] REPORT.xml...

--expect names the suites the run was asked for (repeatable, whitespace- or `-f`-separated,
so a CI area string can be passed through verbatim). Omit it to check attribution only.
Exit: 0 clean, 1 findings, 2 bad usage / unreadable report.
"""

import argparse
import glob
import sys
import xml.etree.ElementTree as ET


def parse_expect(values):
    """Flatten repeated --expect values into a suite list, tolerating `-f suite` tokens."""
    suites = []
    for value in values or []:
        for token in value.split():
            if token == "-f":
                continue
            suites.append(token.removeprefix("-f"))
    return [s for s in suites if s]


def suite_of(testsuite_name):
    """`coverage:test_foo` -> `test_foo`; a bare name is returned unchanged."""
    return testsuite_name.split(":", 1)[1] if ":" in testsuite_name else testsuite_name


def owns(suite, source_file):
    """Report whether source_file sits inside the suite's own directory.

    Matched on a whole path segment so `test_mesh` does not claim `test_mesh_module`, and
    with a leading separator so absolute and relative paths behave the same.
    """
    normalized = "/" + source_file.replace("\\", "/").lstrip("/")
    return f"/{suite}/" in normalized


def collect(paths):
    """Map suite -> list of (case name, source file or None), merged across reports."""
    cases = {}
    for path in paths:
        try:
            # The input is the JUnit report PlatformIO just wrote in this same run, not untrusted
            # data, and defusedxml is not installed for this job.
            # nosemgrep: python.lang.security.use-defused-xml-parse.use-defused-xml-parse
            root = ET.parse(path).getroot()
        except (ET.ParseError, OSError) as exc:
            sys.stderr.write(f"check-test-attribution: cannot read {path}: {exc}\n")
            sys.exit(2)
        # PlatformIO nests <testsuite> under <testsuites>; accept a bare <testsuite> too.
        nodes = [root] if root.tag == "testsuite" else root.iter("testsuite")
        for node in nodes:
            suite = suite_of(node.get("name", ""))
            if not suite:
                continue
            entries = cases.setdefault(suite, [])
            for case in node.iter("testcase"):
                entries.append((case.get("name", "?"), case.get("file")))
    return cases


def main():
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--expect", action="append", default=[])
    parser.add_argument("--label", default="")
    parser.add_argument("reports", nargs="+")
    args = parser.parse_args()

    # Expand globs ourselves: CI passes a pattern that may match nothing if a step was skipped,
    # and a silent pass over zero reports is exactly the false green this script exists to stop.
    paths = sorted({p for pattern in args.reports for p in glob.glob(pattern)})
    if not paths:
        sys.stderr.write(
            "check-test-attribution: no JUnit reports matched %s\n"
            % " ".join(args.reports)
        )
        return 2

    cases = collect(paths)
    expected = parse_expect(args.expect)

    misattributed = []  # (suite, case name, source file)
    unsourced = []  # (suite, case name)
    for suite, entries in sorted(cases.items()):
        for name, source in entries:
            if source is None:
                unsourced.append((suite, name))
            elif not owns(suite, source):
                misattributed.append((suite, name, source))

    empty = [s for s in expected if not cases.get(s)]

    label = f" [{args.label}]" if args.label else ""
    total = sum(len(v) for v in cases.values())
    print(
        f"test attribution{label}: {len(paths)} report(s), "
        f"{len([s for s, v in cases.items() if v])} suite(s) with cases, {total} case(s)"
    )
    if unsourced:
        print("")
        print("UNSOURCED - these cases carry no source file, so ownership cannot be proved:")
        for suite, name in unsourced[:20]:
            print(f"    {suite}: case '{name}'")
        if len(unsourced) > 20:
            print(f"    ... +{len(unsourced) - 20} more")
        print(
            "A report without file attributes is not evidence that the suites ran their own"
        )
        print(
            "tests. Treat it as a finding rather than a pass: the JUnit format has changed, or"
        )
        print("the runner emitted cases it could not attribute.")

    if misattributed:
        print("")
        print(
            "MISATTRIBUTED - these suites reported test cases belonging to another suite."
        )
        print(
            "The run executed one suite's binary under another suite's name; the named"
        )
        print(
            "suites did NOT run. Check for --without-building in the test invocation."
        )
        for suite, name, source in misattributed[:20]:
            print(f"    {suite}: case '{name}' came from {source}")
        if len(misattributed) > 20:
            print(f"    ... +{len(misattributed) - 20} more")

    if empty:
        print("")
        print("EMPTY - these suites were asked to run and produced no test cases:")
        for suite in empty:
            print(f"    {suite}")

    if misattributed or empty or unsourced:
        print("")
        print(
            "RESULT: test attribution FAILED"
            f"{label} ({len(misattributed)} misattributed, {len(empty)} empty,"
            f" {len(unsourced)} unsourced)"
        )
        return 1

    print(f"RESULT: test attribution OK{label}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
