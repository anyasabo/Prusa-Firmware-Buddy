# SBOM tooling

`lib/sbom.yml` lists every piece of third-party code in the firmware: what it
is, which upstream commit it came from, and which images ship it. These tools
check it, turn it into CycloneDX SBOMs, and scan those for known
vulnerabilities, with the triage in `lib/vex.json`.

| Tool | What it does |
| --- | --- |
| `check_sbom_manifest.py` | Pre-commit hook: the manifest matches the tree (`.gitrepo` commits, `bootstrap.py` pins, paths) |
| `gen_sbom.py` | CycloneDX 1.6 SBOM, per image with `--image` |
| `gen_vex.py` | Writes `lib/vex.json` from its triage table; edit the table, not the JSON |
| `scan.sh`, `vex_gate.py` | Grype + VEX on each shipped image; fails on findings the VEX does not decide |
| `match_nvdpp.py`, `sbom_feeds.py` | Extra feeds for CVEs that CPE scanners miss (NVD++, Mbed TLS advisories, GitHub advisories, KEV) |

## Running what CI runs

```sh
utils/sbom/ci.sh          # everything
utils/sbom/ci.sh check    # yapf, ruff, ty, pytest, manifest check, lib/vex.json up to date
utils/sbom/ci.sh scan     # writes SBOMs and Grype results to build-sbom/
```

Needs `uv` and `git`. The checks are the pre-commit hooks, so committing runs
the relevant ones too. Versions are pinned in `.pre-commit-config.yaml`,
`ci.sh` (prek) and `scan.sh` (Grype, with a sha256 per platform).

## When a scan finds something new

Decide it in `gen_vex.py` (`not_affected` with the config option or call site
that rules it out, `affected` with the action, or `investigating`), bump
`REVIEWED`, and run `python utils/sbom/gen_vex.py`.
