#!/usr/bin/env bash
# Scan the shipped firmware images: CycloneDX SBOM per image, Grype with
# lib/vex.json, then fail on findings the VEX does not decide (vex_gate.py).
#
#   utils/sbom/scan.sh [OUT_DIR]     (default: build-sbom/)
#
# Uses exactly GRYPE_VERSION: an installed grype of another version is
# ignored, and the pinned release is downloaded to ~/.cache/buddy-sbom and
# checked against its sha256. Needs uv, curl and git.
set -euo pipefail

GRYPE_VERSION=0.121.1
# sha256 of grype_${GRYPE_VERSION}_<os>_<arch>.tar.gz, from the release's checksums.txt.
grype_sha256() {
    case "$1" in
    linux_amd64) echo bc3ea3f9e6fd56ac781c08c6c5e614271b79f438200757f6932085c19b0a4657 ;;
    linux_arm64) echo 33670612aad351c48cf3c37f4a85d8b043d1b64b21e6034e675bb00b956d78c1 ;;
    darwin_amd64) echo 20fefd09105fb7bf2bbfe29c6bb14edbe4c17214436753735c172465fffe69f6 ;;
    darwin_arm64) echo daa9ffdc0022b34fb476f94cbcabac9d850882c895d83e4a5ecf5e0ab13bce77 ;;
    *) return 1 ;;
    esac
}
PYTHON=3.12
PYYAML=pyyaml==6.0.3
IMAGES=(main esp8266-wifi esp32-wifi)

cd "$(git rev-parse --show-toplevel)"
out=${1:-build-sbom}
mkdir -p "$out"

grype_bin() {
    if command -v grype >/dev/null &&
        [ "$(grype version | awk '$1 == "Version:" { print $2 }')" = "$GRYPE_VERSION" ]; then
        command -v grype
        return
    fi
    local os arch
    os=$(uname -s | tr '[:upper:]' '[:lower:]')
    case "$(uname -m)" in
    x86_64 | amd64) arch=amd64 ;;
    arm64 | aarch64) arch=arm64 ;;
    *) arch=$(uname -m) ;;
    esac
    local sha dir="${XDG_CACHE_HOME:-$HOME/.cache}/buddy-sbom/grype-$GRYPE_VERSION"
    sha=$(grype_sha256 "${os}_${arch}") || {
        echo "scan.sh: no pinned grype for ${os}_${arch}" >&2
        exit 1
    }
    if [ ! -x "$dir/grype" ]; then
        local tarball="grype_${GRYPE_VERSION}_${os}_${arch}.tar.gz" tmp
        tmp=$(mktemp -d)
        curl -fsSLo "$tmp/$tarball" \
            "https://github.com/anchore/grype/releases/download/v${GRYPE_VERSION}/$tarball"
        if command -v sha256sum >/dev/null; then
            echo "$sha  $tmp/$tarball" | sha256sum -c - >&2
        else
            echo "$sha  $tmp/$tarball" | shasum -a 256 -c - >&2
        fi
        mkdir -p "$dir"
        tar -xzf "$tmp/$tarball" -C "$dir" grype
        rm -rf "$tmp"
    fi
    echo "$dir/grype"
}

grype=$(grype_bin)
echo "grype $GRYPE_VERSION: $grype" >&2
results=()
for image in "${IMAGES[@]}"; do
    uv run -q --no-project --python "$PYTHON" --with "$PYYAML" \
        python utils/sbom/gen_sbom.py --image "$image" -o "$out/$image.cdx.json"
    "$grype" -q "sbom:$out/$image.cdx.json" --vex lib/vex.json -o json \
        >"$out/$image.grype.json"
    results+=("$out/$image.grype.json")
done

summary=${GITHUB_STEP_SUMMARY:-/dev/null}
uv run -q --no-project --python "$PYTHON" --with "$PYYAML" python utils/sbom/vex_gate.py \
    "${results[@]}" | tee -a "$summary"
