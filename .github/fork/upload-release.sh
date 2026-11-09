#!/usr/bin/env bash
# Uploads the files of one build label in build/package to the GitHub release of a tag. If the
# release does not exist, it is made as a pre-release. A file with the same name is replaced.
#
# Usage: .github/fork/upload-release.sh TAG LABEL NOTES_FILE
#   TAG: v<version>-anya.<n> for anya/main, or v<version>-pr.<branch>.<n> for a topic branch.
#   The tag must exist on the fork (push it first).
# Environment: GH_REPO (default: anyasabo/Prusa-Firmware-Buddy).
set -euo pipefail

cd "$(dirname "$0")/../.."
tag=${1:?usage: $0 TAG LABEL NOTES_FILE}
label=${2:?usage: $0 TAG LABEL NOTES_FILE}
notes=${3:?usage: $0 TAG LABEL NOTES_FILE}
export GH_REPO=${GH_REPO:-anyasabo/Prusa-Firmware-Buddy}
case "$tag" in v*-anya.* | v*-pr.*) ;; *) echo "not a fork release tag: $tag" >&2; exit 1 ;; esac

files=(build/package/COREONEL_"$label".bbf build/package/COREONEL_"$label".elf build/package/SHA256SUMS-"$label".txt)
for f in "${files[@]}"; do [ -f "$f" ] || { echo "missing: $f" >&2; exit 1; }; done

if ! gh release view "$tag" >/dev/null 2>&1; then
    gh release create "$tag" --verify-tag --prerelease \
        --title "Firmware ${tag#v} for the CORE One L (fork anyasabo/Prusa-Firmware-Buddy)" \
        --notes-file "$notes"
fi
gh release upload "$tag" "${files[@]}" --clobber
gh release view "$tag" --json url,assets --jq '.url, .assets[].name'
