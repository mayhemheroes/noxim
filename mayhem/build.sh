#!/usr/bin/env bash
#
# mayhem/build.sh — build noxim's fuzz targets + the functional-test oracle, ON TOP of the org
# C/C++ base image (ghcr.io/mayhemheroes/base). The base exports the build contract:
#   CC, CXX, LIB_FUZZING_ENGINE (-fsanitize=fuzzer), STANDALONE_FUZZ_MAIN (run-once driver),
#   SANITIZER_FLAGS (ASan+UBSan, both halting), SRC (/mayhem). We default them so the script also
#   runs outside the image.
#
# Targets fuzz noxim's traffic-table tooling (other/ttable_from_hub.cpp):
#   /mayhem/ttable_from_hub      — file-input target: the upstream CLI, instrumented. Parses a
#                                  traffic-table file (argv[1]); reaches the region/distance math,
#                                  asserts, malloc-sizing and the ParseTrafficTable read loop.
#   /mayhem/fuzz                 — libFuzzer harness over the leak-free routing/region helpers.
#   /mayhem/fuzz-standalone      — same harness on LLVM's run-once driver (crash reproducer).
#   /mayhem/ttable_unit_test     — known-answer oracle (normal flags, no sanitizers), run by test.sh.
set -euo pipefail

: "${SRC:=/mayhem}"
: "${CC:=clang}"
: "${CXX:=clang++}"
: "${SANITIZER_FLAGS:=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
export DEBUG_FLAGS

OTHER="$SRC/other"
TTABLE_SRC="$OTHER/ttable_from_hub.cpp"
FDP_INC="$($CC -print-resource-dir)/include/fuzzer"   # FuzzedDataProvider.h ships here

# ttable_from_hub.cpp's main() uses time(); upstream compiled it with g++ (transitive include).
# Inject <ctime> on the command line for the direct build rather than editing the upstream file.
CXXSTD="-std=c++11"
COMPAT="-include ctime"

echo "== [1/4] file-input target: ttable_from_hub (instrumented) =="
# shellcheck disable=SC2086
"$CXX" $CXXSTD $COMPAT $SANITIZER_FLAGS $DEBUG_FLAGS "$TTABLE_SRC" -o "$SRC/ttable_from_hub"

echo "== [2/4] libFuzzer harness: fuzz =="
# shellcheck disable=SC2086
"$CXX" $CXXSTD $SANITIZER_FLAGS $DEBUG_FLAGS \
    -I"$OTHER" -I"$FDP_INC" \
    "$SRC/mayhem/fuzz.cpp" \
    $LIB_FUZZING_ENGINE \
    -o "$SRC/fuzz"

echo "== [3/4] standalone reproducer: fuzz-standalone =="
# Compile LLVM's run-once driver as a C object first, then link the C++ harness against it
# (clang++ would otherwise mangle the C `LLVMFuzzerTestOneInput` reference).
# shellcheck disable=SC2086
"$CC" $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o /tmp/standalone_main.o
# shellcheck disable=SC2086
"$CXX" $CXXSTD $SANITIZER_FLAGS $DEBUG_FLAGS \
    -I"$OTHER" -I"$FDP_INC" \
    "$SRC/mayhem/fuzz.cpp" \
    /tmp/standalone_main.o \
    -o "$SRC/fuzz-standalone"

echo "== [4/4] test oracle: ttable_unit_test (normal flags, no sanitizers) =="
# shellcheck disable=SC2086
"$CXX" $CXXSTD -O2 -I"$OTHER" "$SRC/mayhem/ttable_unit_test.cpp" -o "$SRC/ttable_unit_test"

echo "== build.sh done =="
ls -la "$SRC/ttable_from_hub" "$SRC/fuzz" "$SRC/fuzz-standalone" "$SRC/ttable_unit_test"
