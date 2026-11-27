#!/bin/sh
# Run the checks of .clang-tidy on the lines that differ from BASE (default origin/master... the merge
# base), in src/ and lib/Marlin/. Old findings on lines that the change does not touch do not count.
# Usage: utils/clang_tidy_diff.sh BUILD_DIR [BASE]
#   BUILD_DIR: a firmware build directory with compile_commands.json (for example build/coreonel_release_boot)
# Environment: CLANG_TIDY (default clang-tidy), CLANG_TIDY_DIFF (default: next to the clang-tidy binary
# in the LLVM share directory), JOBS (default: the number of CPUs).
# Exit status: 0 with no finding, 1 with findings or an error.
set -eu
build=${1:?usage: $0 BUILD_DIR [BASE]}
base=${2:-$(git merge-base HEAD origin/master)}
root=$(git rev-parse --show-toplevel)
tidy=${CLANG_TIDY:-clang-tidy}
llvm_dir=$(dirname "$(readlink -f "$(command -v "$tidy")")")/..
diff_py=${CLANG_TIDY_DIFF:-$llvm_dir/share/clang/clang-tidy-diff.py}
db=$(mktemp -d)
trap 'rm -rf "$db"' EXIT
python3 "$root/utils/clang_tidy_db.py" "$build" "$db" > /dev/null
out=$(cd "$root" && git diff -U0 "$base" -- 'src/*.c' 'src/*.cpp' 'src/*.h' 'src/*.hpp' 'lib/Marlin/*.c' 'lib/Marlin/*.cpp' 'lib/Marlin/*.h' 'lib/Marlin/*.hpp' |
    python3 "$diff_py" -p1 -path "$db" -clang-tidy-binary "$tidy" -j "${JOBS:-$(nproc)}" -quiet 2>&1) || true
findings=$(printf '%s\n' "$out" | grep -E '^/.*: (warning|error): ' || true)
if [ -n "$findings" ]; then
    printf '%s\n' "$out"
    echo "clang-tidy: $(printf '%s\n' "$findings" | wc -l) finding(s) on changed lines" >&2
    exit 1
fi
echo "clang-tidy: no finding on changed lines"
