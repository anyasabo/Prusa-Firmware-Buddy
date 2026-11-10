#!/usr/bin/env bash
# Builds a CORE One L release image (.bbf with the bootloader) of a source tree, for a fork release.
# The source tree can be anya/main or a topic branch worktree: a topic branch has no fork files, so
# this script takes the tree as an argument.
#
# Usage: .github/fork/build.sh SRC_DIR LABEL
#   LABEL goes into the version after the build number: 9.0.0+<build>.<LABEL> (for example anya.1).
#   The version stays the upstream version, so the printer and Connect compare versions as usual.
# Output: build/package/COREONEL_<LABEL>.bbf and .elf (in this tree), and SHA256SUMS-<LABEL>.txt.
# On nasty: run it under the devspace lock (flock /workspaces/prusa/.heavy.lock).
set -euo pipefail

here=$(cd "$(dirname "$0")/../.." && pwd)
src=$(cd "${1:?usage: $0 SRC_DIR LABEL}" && pwd)
label=${2:?usage: $0 SRC_DIR LABEL}
case "$label" in *[!A-Za-z0-9.-]*) echo "LABEL: only A-Z a-z 0-9 . -" >&2; exit 1 ;; esac

cd "$src"
build=$(git rev-list --count HEAD)
jobs=$(build-jobs 2>/dev/null || nproc)
py=python3
[ -x .venv/bin/python ] && py=.venv/bin/python
"$py" utils/build.py --preset coreonel --build-type release --bootloader yes \
    --version-suffix "+$build.$label" --version-suffix-short "+$build" --no-build
# The ninja that CMake configured. With two ninja versions in .dependencies, a glob can find the other one.
ninja=$(sed -n 's/^CMAKE_MAKE_PROGRAM:[A-Z]*=//p' build/coreonel_release_boot/CMakeCache.txt)
"${ninja:-ninja}" -C build/coreonel_release_boot -j "$jobs"

out="$here/build/package"
mkdir -p "$out"
cp build/coreonel_release_boot/firmware.bbf "$out/COREONEL_$label.bbf"
cp build/coreonel_release_boot/firmware "$out/COREONEL_$label.elf"
(cd "$out" && sha256sum "COREONEL_$label.bbf" "COREONEL_$label.elf" > "SHA256SUMS-$label.txt")
strings build/coreonel_release_boot/firmware | grep -m1 -E "^9\.[0-9]+\.[0-9]+\+$build\." || true
cat "$out/SHA256SUMS-$label.txt"
