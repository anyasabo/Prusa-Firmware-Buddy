#!/bin/sh
# Run the checks of .clang-tidy on the lines that differ from BASE, in src/ and lib/Marlin/. Old findings
# on lines that the change does not touch do not count.
# Usage: utils/clang_tidy_diff.sh BUILD_DIR [BASE]
#   BUILD_DIR: a firmware build directory with compile_commands.json (for example build/coreonel_release_boot)
#   BASE: default the merge base with upstream/master, else with origin/master
# Environment: CLANG_TIDY (default clang-tidy), CLANG_TIDY_DIFF (default clang-tidy-diff-NN.py or
# clang-tidy-diff.py on PATH, else the copy in the LLVM share directory), JOBS (default: the CPU count).
# Exit status: 0 with no finding, 1 with findings or when a tool fails.
set -eu
fail() {
    echo "clang_tidy_diff: $*" >&2
    exit 1
}
build=${1:?usage: $0 BUILD_DIR [BASE]}
root=$(git rev-parse --show-toplevel)
if [ $# -ge 2 ]; then
    base=$2
else
    base=
    for ref in upstream/master origin/master; do
        if git rev-parse -q --verify "$ref^{commit}" > /dev/null; then
            base=$(git merge-base HEAD "$ref")
            break
        fi
    done
    [ -n "$base" ] || fail "no upstream/master or origin/master: give BASE as the second argument"
fi
tidy=${CLANG_TIDY:-clang-tidy}
command -v "$tidy" > /dev/null || fail "$tidy not found (set CLANG_TIDY)"
diff_py=${CLANG_TIDY_DIFF:-$(command -v "clang-tidy-diff${tidy#clang-tidy}.py" || command -v clang-tidy-diff.py || echo "$(dirname "$(readlink -f "$(command -v "$tidy")")")/../share/clang/clang-tidy-diff.py")}
[ -f "$diff_py" ] || fail "clang-tidy-diff.py not found (set CLANG_TIDY_DIFF)"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 "$root/utils/clang_tidy_db.py" "$build" "$work/db" > /dev/null
(cd "$root" && git diff -U0 "$base" -- 'src/*.c' 'src/*.cpp' 'src/*.h' 'src/*.hpp' 'lib/Marlin/*.c' 'lib/Marlin/*.cpp' 'lib/Marlin/*.h' 'lib/Marlin/*.hpp') > "$work/diff"
status=0
out=$(python3 "$diff_py" -p1 -path "$work/db" -clang-tidy-binary "$tidy" -j "${JOBS:-$(nproc)}" -quiet < "$work/diff" 2>&1) || status=$?
findings=$(printf '%s\n' "$out" | grep -cE '^/.*: (warning|error): ' || true)
if [ "$findings" -gt 0 ]; then
    printf '%s\n' "$out"
    fail "$findings finding(s) on changed lines"
fi
if [ "$status" -ne 0 ]; then
    printf '%s\n' "$out"
    fail "clang-tidy-diff.py failed (exit $status)"
fi
echo "clang-tidy: no finding on changed lines"
