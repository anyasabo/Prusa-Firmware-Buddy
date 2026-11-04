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
from collections.abc import Collection
from pathlib import Path
from typing import Any

from sbom_manifest import ROOT, Entry, load_components, scope

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
    t = datetime.datetime.fromtimestamp(int(epoch), datetime.UTC)
    return t.strftime("%Y-%m-%dT%H:%M:%SZ")


def as_list(value: Any) -> list[Any]:
    if value is None:
        return []
    return value if isinstance(value, list) else [value]


def patch_text(patch: str | dict[str, str]) -> str:
    if isinstance(patch, dict):
        ((path, what), ) = patch.items()
        return f"{path}: {what}"
    return str(patch)


def component(path: str, entry: Entry) -> dict[str, Any]:
    comp: dict[str, Any] = {
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

    refs = []
    if entry.get("repository"):
        refs.append({"type": "vcs", "url": entry["repository"]})
    if entry.get("url"):
        refs.append({"type": "distribution", "url": entry["url"]})
    comp["externalReferences"] = refs

    pedigree: dict[str, Any] = {}
    if entry.get("upstream_commit"):
        pedigree["commits"] = [{"uid": entry["upstream_commit"]}]
    # The hash of what ships wins; otherwise the upstream release archive.
    digest = entry.get("shipped_sha256") or entry.get("upstream_sha256")
    if digest:
        comp["hashes"] = [{"alg": "SHA-256", "content": digest}]
    if scope(entry) != "shipped":
        comp["scope"] = "excluded"
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
    properties.append({"name": "buddy:scope", "value": scope(entry)})
    properties += [{
        "name": "buddy:image",
        "value": image
    } for image in entry.get("images", [])]
    if b := entry.get("bootstrap"):
        dependency = "/".join(
            filter(None, [b["dependency"], b.get("submodule")]))
        properties.append({"name": "buddy:bootstrap", "value": dependency})
    comp["properties"] = properties
    return comp


def selected(entry: Entry, scopes: Collection[str], image: str | None) -> bool:
    if scope(entry) not in scopes:
        return False
    # Entries without `images` are assumed to be in the main firmware.
    return image is None or image in entry.get("images", ["main"])


def generate(components: dict[str, Entry],
             scopes: Collection[str] = ("shipped", ),
             image: str | None = None) -> dict[str, Any]:
    version = (ROOT / "version.txt").read_text().strip()
    head = git("rev-parse", "HEAD")
    selected_components = [
        component(path, entry) for path, entry in sorted(components.items())
        if selected(entry, scopes, image)
    ]
    body = json.dumps(selected_components, sort_keys=True)
    serial = uuid.uuid5(NAMESPACE, f"{version}\0{head}\0{body}")
    repo = {
        "type": "vcs",
        "url": "https://github.com/prusa3d/Prusa-Firmware-Buddy",
        "comment": head,
    }
    firmware = {
        "type": "firmware",
        "bom-ref": "buddy",
        "name": "Prusa-Firmware-Buddy",
        "version": version,
        "supplier": {
            "name": "Prusa Research a.s."
        },
        "externalReferences": [repo],
    }
    tool = {
        "type": "application",
        "name": "gen_sbom.py",
        "group": "Prusa-Firmware-Buddy",
    }
    metadata = {
        "timestamp": build_time(),
        "tools": {
            "components": [tool]
        },
        "component": firmware,
    }
    refs = [c["bom-ref"] for c in selected_components]
    return {
        "bomFormat": "CycloneDX",
        "specVersion": "1.6",
        "serialNumber": f"urn:uuid:{serial}",
        "version": 1,
        "metadata": metadata,
        "components": selected_components,
        "dependencies": [{
            "ref": "buddy",
            "dependsOn": refs
        }],
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-o",
                        "--output",
                        type=Path,
                        help="output file (default: stdout)")
    parser.add_argument("--image",
                        help="only components in this firmware image (e.g. "
                        "main, esp8266-wifi, bootloader)")
    parser.add_argument("--include",
                        action="append",
                        choices=["test", "tool"],
                        default=[],
                        help="also list test-only or tool components, "
                        "marked scope=excluded")
    args = parser.parse_args()

    sbom = generate(load_components(), {"shipped", *args.include}, args.image)
    text = json.dumps(sbom, indent=2) + "\n"
    if args.output:
        args.output.write_text(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
