#!/bin/sh
# Line coverage of the host unit tests with clang source-based coverage.
#
#   utils/coverage_tests.sh [BUILD_DIR [PRINTER [BOARD]]]
#
# Defaults: build/tests_llvmcov, MINI, BUDDY (the configuration of utils/build_tests.py).
# The build type is the default of build_tests.py (no CMAKE_BUILD_TYPE: -Os with assertions).
# Source-based coverage stays exact with optimization. Debug turns on NETWORKING_BENCHMARK_ENABLED,
# and nhttp_tests then does not link.
# Environment: LLVM_SUFFIX (default -23, for clang-23, llvm-cov-23, ...), JOBS, CTEST_ARGS, KEEP_GOING,
# EXTRA_CMAKE_ARGS (for example -DRESOURCES=NO for PRINTER=COREONEL). The script configures only a
# new BUILD_DIR. To change PRINTER, BOARD or EXTRA_CMAKE_ARGS, use a new BUILD_DIR.
# File names are relative to the source tree (-ffile-prefix-map), with BUILD_DIR/.. in front for
# the files that CMake gives as relative paths.
# Output in BUILD_DIR: tests.profdata, coverage-report.txt (llvm-cov report, one line per file),
# coverage.lcov (llvm-cov export, line counts) and coverage-objects.txt (the binaries).
#
# utils/build_tests.py --coverage gives a gcov/gcovr report with GCC. This script uses clang, so
# the counts come from the same instrumentation as the fuzz harnesses.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
build=$(realpath -m "${1:-$root/build/tests_llvmcov}")
printer=${2:-MINI}
board=${3:-BUDDY}
sfx=${LLVM_SUFFIX:--23}
jobs=${JOBS:-$(build-jobs 2>/dev/null || nproc)}

# The project venv of utils/bootstrap.py, and its CMake and Ninja.
[ -x "$root/.venv/bin/python3" ] && PATH="$root/.venv/bin:$PATH"
for t in cmake ninja; do
    d=$(python3 "$root/utils/bootstrap.py" --print-dependency-directory $t)
    PATH="$d/bin:$d:$PATH"
done
export PATH

cov="-fprofile-instr-generate -fcoverage-mapping"
if [ ! -f "$build/CMakeCache.txt" ]; then
    # shellcheck disable=SC2086 # EXTRA_CMAKE_ARGS is a list of words
    cmake -S "$root" -B "$build" -G Ninja -DPRINTER="$printer" -DBOARD="$board" ${EXTRA_CMAKE_ARGS:-} \
        -DCMAKE_C_COMPILER="clang$sfx" -DCMAKE_CXX_COMPILER="clang++$sfx" \
        -DCMAKE_C_FLAGS="$cov" -DCMAKE_CXX_FLAGS="$cov" \
        -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld$sfx $cov"
elif ! grep -qx "PRINTER:STRING=$printer" "$build/CMakeCache.txt"; then
    echo "$build is configured for another PRINTER. Use a new BUILD_DIR." >&2
    exit 1
fi

# catch_discover_tests runs each test binary at build time. Keep those profiles out of the result.
# KEEP_GOING=1: build the test binaries that compile, and measure only those. For a PRINTER that
# upstream does not build tests for, some tests do not compile.
if [ "${KEEP_GOING:-0}" = 1 ]; then
    LLVM_PROFILE_FILE="$build/profiles-discover/%m.profraw" cmake --build "$build" -j "$jobs" --target tests -- -k 0 \
        > "$build/build.log" 2>&1 || echo "Some targets failed, see $build/build.log" >&2
else
    LLVM_PROFILE_FILE="$build/profiles-discover/%m.profraw" cmake --build "$build" -j "$jobs" --target tests
fi

rm -rf "$build/profiles"
mkdir -p "$build/profiles"
# %4m: a pool of 4 files for each binary, merged online, so thousands of test processes give few
# files. A test failure does not stop the report.
# shellcheck disable=SC2086 # CTEST_ARGS is a list of words
LLVM_PROFILE_FILE="$build/profiles/%4m.profraw" ctest --test-dir "$build" -j "$jobs" \
    --output-on-failure ${CTEST_ARGS:-} > "$build/ctest.log" 2>&1 || echo "Some tests failed, see $build/ctest.log" >&2
tail -n 3 "$build/ctest.log"

"llvm-profdata$sfx" merge -sparse "$build"/profiles/*.profraw -o "$build/tests.profdata"

# Each test binary that ctest ran.
ctest --test-dir "$build" --show-only=json-v1 \
    | python3 -c 'import json, sys; print("\n".join(sorted({t["command"][0] for t in json.load(sys.stdin)["tests"] if t.get("command")})))' \
    | while read -r f; do if [ -x "$f" ]; then echo "$f"; fi; done \
    > "$build/coverage-objects.txt"

# The file names are relative (-ffile-prefix-map). Leave out the tests, Catch2 and system headers.
ignore='(^|/)(tests|lib/Catch2)/|\.dependencies/|^/usr/'
set --
first=1
while read -r obj; do
    if [ $first = 1 ]; then set -- "$obj"; first=0; else set -- "$@" -object "$obj"; fi
done < "$build/coverage-objects.txt"
# llvm-cov warns "N functions have mismatched data" (approximately 1600 with MINI) and counts
# those functions as not run. The binaries compile some functions with different options (lwIP,
# for example). One export for each binary gave the same result (0.2 % more covered lines).
"llvm-cov$sfx" report -instr-profile="$build/tests.profdata" -ignore-filename-regex="$ignore" \
    "$@" > "$build/coverage-report.txt" 2> "$build/llvm-cov.log"
"llvm-cov$sfx" export -format=lcov -instr-profile="$build/tests.profdata" \
    -ignore-filename-regex="$ignore" "$@" > "$build/coverage.lcov" 2>> "$build/llvm-cov.log"
tail -n 1 "$build/coverage-report.txt"
