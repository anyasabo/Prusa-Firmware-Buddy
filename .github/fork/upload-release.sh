#!/usr/bin/env bash
# Uploads the files of one build label in build/package to the GitHub release of a tag. If the
# release does not exist, it is made as a pre-release. A file with the same name is replaced.
# The files are the ones that SHA256SUMS-<LABEL>.txt lists, and that file.
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

sums=build/package/SHA256SUMS-$label.txt
[ -f "$sums" ] || { echo "missing: $sums" >&2; exit 1; }
(cd build/package && sha256sum --check --quiet "SHA256SUMS-$label.txt")
files=("$sums")
while read -r _ name; do files+=("build/package/$name"); done < "$sums"

if ! gh release view "$tag" >/dev/null 2>&1; then
    gh release create "$tag" --verify-tag --prerelease \
        --title "Firmware ${tag#v} (fork anyasabo/Prusa-Firmware-Buddy)" \
        --notes-file "$notes"
fi
gh release upload "$tag" "${files[@]}" --clobber
gh release view "$tag" --json url,assets --jq '.url, .assets[].name'
