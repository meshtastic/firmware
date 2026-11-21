# shellcheck shell=bash
#
# The one definition of "this output carries a sanitizer fault". Sourced by bin/run-tests.sh (which
# scans a whole native test run) and bin/check-sanitizer-log.sh (which CI points at the simulator's
# own output). Nothing here executes on its own.
#
# One copy because the two callers must agree: a pattern that matches in the local harness and not
# in CI is a fault that only ever appears on someone's laptop, and the reverse is a red CI run
# nobody can reproduce.
#
# The coverage build is sanitizer-instrumented and aborts NON-ZERO at exit on a fault - most often a
# LeakSanitizer leak - AFTER every test has already printed [PASSED]. pio then reports
# [ERRORED]/SIGHUP with no :FAIL: anywhere, so it masquerades as a phantom "N-1 of N succeeded".
# See .notes/test-passfail-filter.md.
#
# Match only real FAULT lines, never the benign "AddressSanitizer: failed to intercept '...'"
# startup noise that prints on every sanitizer run (it'd mislabel a normal [FAILED] as a leak).
# Formats per LLVM/Google sanitizer docs: ASan/LSan emit "==PID==ERROR: <San>: ...", UBSan emits
# "file:line:col: runtime error: ...", TSan emits "WARNING: ThreadSanitizer: ..."; all close with
# a "SUMMARY: <San>: ..." line (LSan-under-ASan reports its SUMMARY as "AddressSanitizer").
SAN_RE='(ERROR|WARNING): (Address|Leak|Thread|UndefinedBehavior)Sanitizer:|SUMMARY: (Address|Leak|Thread|UndefinedBehavior)Sanitizer:|Direct leak of|Indirect leak of|detected memory leaks|heap-use-after-free|heap-buffer-overflow|stack-buffer-overflow|attempting double-free|LeakSanitizer has encountered a fatal error|runtime error:'
