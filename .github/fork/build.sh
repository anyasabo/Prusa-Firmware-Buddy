#!/usr/bin/env bash
# Builds release images (.bbf with the bootloader) of a source tree, for a fork release.
# The source tree can be anya/main or a topic branch worktree: a topic branch has no fork files, so
# this script takes the tree as an argument.
#
# Usage: .github/fork/build.sh SRC_DIR LABEL [PRESET ...]
#   LABEL goes into the version after the build number: 8.0.2+<build>.<LABEL> (for example anya.2).
#   The version stays the upstream version, so the printer and Connect compare versions as usual.
#   PRESET: a key of the table below, or "all" for each printer that has a file in the upstream
#   release. The default is coreonel.
# Output, in build/package of this tree: <PREFIX>_<LABEL>.bbf and .elf for each preset, and
# SHA256SUMS-<LABEL>.txt for all the files of the label.
# On nasty: run it under the devspace lock (flock /workspaces/prusa/.heavy.lock).
set -euo pipefail

# The file name prefix of each preset. The prefixes are the names of the upstream release files
# (https://github.com/prusa3d/Prusa-Firmware-Buddy/releases/tag/v8.0.2), so that a user can find
# the file for a printer. The CORE One L keeps COREONEL, the name of the earlier fork releases.
# The main image of a printer contains the firmware of its puppy boards (CMakeLists.txt,
# add_puppy_firmware), so one preset for each printer is sufficient.
declare -A prefix=(
    [coreonel]=COREONEL
    [coreone]=COREONE_COREONE+GEN2
    [coreone_indx]=COREONE_COREONE+GEN2_INDX
    [mk4]=MK4_MK4S_MK3.9_MK3.9S
    [mk3.5]=MK3.5_MK3.5S
    [xl]=XL_XL+
    [mini-en-cs]=MINI_english-czech
    [mini-en-de]=MINI_english-german
    [mini-en-es]=MINI_english-spanish
    [mini-en-fr]=MINI_english-french
    [mini-en-it]=MINI_english-italian
    [mini-en-ja]=MINI_english-japanese
    [mini-en-pl]=MINI_english-polish
    [mini-en-uk]=MINI_english-ukrainian
)
all=(coreonel coreone coreone_indx mk4 mk3.5 xl
    mini-en-cs mini-en-de mini-en-es mini-en-fr mini-en-it mini-en-ja mini-en-pl mini-en-uk)

here=$(cd "$(dirname "$0")/../.." && pwd)
src=$(cd "${1:?usage: $0 SRC_DIR LABEL [PRESET ...]}" && pwd)
label=${2:?usage: $0 SRC_DIR LABEL [PRESET ...]}
shift 2
case "$label" in *[!A-Za-z0-9.-]*) echo "LABEL: only A-Z a-z 0-9 . -" >&2; exit 1 ;; esac
presets=("${@:-coreonel}")
[ "${presets[*]}" = all ] && presets=("${all[@]}")
out="$here/build/package"
for p in "${presets[@]}"; do
    [ -n "${prefix[$p]:-}" ] || { echo "unknown preset: $p (known: ${all[*]})" >&2; exit 1; }
    # The label goes into the version, and a published file must not change. Use a new label for a
    # new build.
    [ ! -e "$out/${prefix[$p]}_$label.bbf" ] || { echo "exists: $out/${prefix[$p]}_$label.bbf (use a new label)" >&2; exit 1; }
done

cd "$src"
build=$(git rev-list --count HEAD)
# Reproducible images: GCC takes __DATE__ and __TIME__ from SOURCE_DATE_EPOCH
# (https://reproducible-builds.org/docs/source-date-epoch/). Marlin prints "Compiled: " __DATE__
# at boot (lib/Marlin/Marlin/src/Marlin.cpp). With the commit time, two builds of one commit give
# the same bytes. Without it, they differ in that string and in the digest of the .bbf.
: "${SOURCE_DATE_EPOCH:=$(git log -1 --format=%ct HEAD)}"
export SOURCE_DATE_EPOCH
jobs=$(build-jobs 2>/dev/null || nproc)
py=python3
[ -x .venv/bin/python ] && py=.venv/bin/python
mkdir -p "$out"

for p in "${presets[@]}"; do
    dir=build/${p}_release_boot
    # A fresh directory: ninja does not track SOURCE_DATE_EPOCH, so an old directory can keep the
    # __DATE__ of an earlier build. ccache keeps the rebuild fast.
    rm -rf -- "${dir:?}"
    "$py" utils/build.py --preset "$p" --build-type release --bootloader yes \
        --version-suffix "+$build.$label" --version-suffix-short "+$build" --no-build
    # The ninja that CMake configured. With two ninja versions in .dependencies, a glob can find the other one.
    ninja=$(sed -n 's/^CMAKE_MAKE_PROGRAM:[A-Z]*=//p' "$dir/CMakeCache.txt")
    "${ninja:-ninja}" -C "$dir" -j "$jobs"
    cp "$dir/firmware.bbf" "$out/${prefix[$p]}_$label.bbf"
    cp "$dir/firmware" "$out/${prefix[$p]}_$label.elf"
    strings "$dir/firmware" | grep -m1 -E "^[0-9]+\.[0-9]+\.[0-9]+\+$build\." || true
done

# One checksum file for all the files of the label, also the files of an earlier run.
(cd "$out" && sha256sum -- *_"$label".bbf *_"$label".elf > "SHA256SUMS-$label.txt")
cat "$out/SHA256SUMS-$label.txt"
