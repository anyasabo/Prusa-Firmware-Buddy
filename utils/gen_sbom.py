#!/usr/bin/env python3
"""Generate a CycloneDX 1.6 SBOM for the firmware from lib/sbom.yml.

The output lists every vendored component in the manifest, with its upstream
identity (CPE, purl, upstream commit) and local patches as pedigree. It is
meant to be fed to a vulnerability scanner, for example

    python utils/gen_sbom.py -o buddy.cdx.json
    grype sbom:buddy.cdx.json --vex lib/vex.json

The output is deterministic for a given source tree: the serial number is
derived from the content, and the timestamp comes from SOURCE_DATE_EPOCH or
the HEAD commit date.

Needs pyyaml.
"""
from __future__ import annotations

import argparse
import datetime
import json
import os
import subprocess
import sys
import uuid
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "lib" / "sbom.yml"
NAMESPACE = uuid.UUID("5b0f2f6e-6a0e-4f8c-9d4e-62757464792d")


def git(*args: str) -> str:
    return subprocess.run(["git", *args],
                          cwd=ROOT,
                          capture_output=True,
                          text=True,
                          check=True).stdout.strip()


def build_time() -> str:
    epoch = os.environ.get("SOURCE_DATE_EPOCH") or git("log", "-1",
                                                       "--format=%ct")
    t = datetime.datetime.fromtimestamp(int(epoch), datetime.timezone.utc)
    return t.strftime("%Y-%m-%dT%H:%M:%SZ")


def as_list(value) -> list:
    if value is None:
        return []
    return value if isinstance(value, list) else [value]


def patch_text(patch) -> str:
    if isinstance(patch, dict):
        ((path, what), ) = patch.items()
        return f"{path}: {what}"
    return str(patch)


def component(path: str, entry: dict) -> dict:
    comp = {
        "type": "library",
        "bom-ref": path,
        "name": entry["name"],
    }
    if entry.get("version"):
        comp["version"] = str(entry["version"])
    comp["supplier"] = {"name": entry["supplier"].split(": ", 1)[1]}
    comp["licenses"] = [{"expression": entry["license"]}]

    cpes = as_list(entry.get("cpe"))
    if cpes:
        comp["cpe"] = cpes[0]
    if entry.get("purl"):
        comp["purl"] = entry["purl"]

    comp["externalReferences"] = [{"type": "vcs", "url": entry["upstream"]}]

    pedigree = {}
    if entry.get("upstream_commit"):
        pedigree["commits"] = [{"uid": entry["upstream_commit"]}]
    if entry.get("upstream_sha256"):
        comp["hashes"] = [{
            "alg": "SHA-256",
            "content": entry["upstream_sha256"]
        }]
    patches = entry.get("patches") or []
    if patches:
        pedigree["patches"] = [{
            "type": "unofficial",
            "diff": {
                "text": {
                    "content": patch_text(p)
                }
            }
        } for p in patches]
    if entry.get("note"):
        pedigree["notes"] = " ".join(entry["note"].split())
    if pedigree:
        comp["pedigree"] = pedigree

    properties = [{"name": "syft:cpe23", "value": c} for c in cpes[1:]]
    properties.append({"name": "buddy:path", "value": path})
    if entry.get("subrepo_commit"):
        properties.append({
            "name": "buddy:subrepo_commit",
            "value": entry["subrepo_commit"]
        })
    if entry.get("first_party"):
        properties.append({"name": "buddy:first_party", "value": "true"})
    comp["properties"] = properties
    return comp


def generate(manifest: dict) -> dict:
    version = (ROOT / "version.txt").read_text().strip()
    head = git("rev-parse", "HEAD")
    components = [
        component(path, entry)
        for path, entry in sorted(manifest["components"].items())
    ]
    body = json.dumps(components, sort_keys=True)
    serial = uuid.uuid5(NAMESPACE, f"{version}\0{head}\0{body}")
    return {
        "bomFormat":
        "CycloneDX",
        "specVersion":
        "1.6",
        "serialNumber":
        f"urn:uuid:{serial}",
        "version":
        1,
        "metadata": {
            "timestamp": build_time(),
            "tools": {
                "components": [{
                    "type": "application",
                    "name": "gen_sbom.py",
                    "group": "Prusa-Firmware-Buddy",
                }]
            },
            "component": {
                "type":
                "firmware",
                "bom-ref":
                "buddy",
                "name":
                "Prusa-Firmware-Buddy",
                "version":
                version,
                "supplier": {
                    "name": "Prusa Research a.s."
                },
                "externalReferences": [{
                    "type": "vcs",
                    "url": "https://github.com/prusa3d/Prusa-Firmware-Buddy",
                    "comment": head,
                }],
            },
        },
        "components":
        components,
        "dependencies": [{
            "ref": "buddy",
            "dependsOn": [c["bom-ref"] for c in components]
        }],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("-o",
                        "--output",
                        type=Path,
                        help="output file (default: stdout)")
    args = parser.parse_args()

    manifest = yaml.safe_load(MANIFEST.read_text())
    text = json.dumps(generate(manifest), indent=2) + "\n"
    if args.output:
        args.output.write_text(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
