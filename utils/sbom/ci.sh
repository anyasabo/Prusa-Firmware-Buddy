#!/usr/bin/env bash
# Run what .github/workflows/sbom.yml runs, the same way.
#
#   utils/sbom/ci.sh            checks, then the scan
#   utils/sbom/ci.sh check      yapf, ruff, ty, pytest, manifest, VEX up to date
#   utils/sbom/ci.sh scan       Grype + VEX on the shipped images (scan.sh)
#
# The checks are the pre-commit hooks below, so tool versions live only in
# .pre-commit-config.yaml; yapf and gen-vex rewrite files and fail, like on
# commit. Needs uv and git.
set -euo pipefail

PREK_VERSION=0.5.10
HOOKS=(yapf ruff-check ty-sbom sbom-pytest check-sbom-manifest gen-vex)

cd "$(git rev-parse --show-toplevel)"

check() {
    local files
    # Tracked and new (untracked, not ignored) files.
    files=$(git ls-files --cached --others --exclude-standard utils/sbom tests/sbom \
        lib/sbom.yml lib/sbom.schema.json lib/vex.json utils/bootstrap.py pyproject.toml)
    # shellcheck disable=SC2086 # one path per word
    uvx "prek@$PREK_VERSION" run "${HOOKS[@]}" --files $files
}

case "${1:-all}" in
check) check ;;
scan) utils/sbom/scan.sh ;;
all) check && utils/sbom/scan.sh ;;
*)
    echo "usage: $0 [check|scan]" >&2
    exit 2
    ;;
esac
