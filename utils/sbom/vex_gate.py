#!/usr/bin/env python3
"""Report Grype findings and fail on those that lib/vex.json does not decide.

    python utils/sbom/vex_gate.py out/main.grype.json out/esp32-wifi.grype.json

Each argument is `grype ... --vex lib/vex.json -o json` output for one image,
named <image>.grype.json. Grype already drops not_affected findings; the rest
are listed with their VEX status (affected, under_investigation) or as NEW.
A finding counts as decided when the VEX has a statement for its CVE, or any
related ID, on the same product purl. Prints Markdown (used as the GitHub job
summary). Exit status 1 when there is a NEW finding.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

from sbom_manifest import ROOT

VEX = ROOT / "lib" / "vex.json"


def vex_status() -> dict[tuple[str, str], str]:
    """(vulnerability, product purl) -> status."""
    statements = json.loads(VEX.read_text())["statements"]
    return {
        (s["vulnerability"]["name"], p["@id"]): s["status"]
        for s in statements
        for p in s["products"]
    }


def report(image: str, matches: list[dict[str, Any]],
           status: dict[tuple[str, str], str]) -> tuple[list[str], int]:
    """Markdown lines for one image, and the number of NEW findings."""
    lines = [f"### {image}", ""]
    if not matches:
        lines.append("No open findings.")
    new = 0
    for m in matches:
        artifact, vuln = m["artifact"], m["vulnerability"]
        purl = artifact["purl"]
        related = [r["id"] for r in m.get("relatedVulnerabilities", [])]
        ids = [vuln["id"], *related]
        known = next((status[i, purl] for i in ids if (i, purl) in status),
                     None)
        new += known is None
        lines.append(f"- {known or '**NEW**'}: {', '.join(ids)} in "
                     f"{artifact['name']} {artifact['version']} "
                     f"({vuln.get('severity', '?')})")
    lines.append("")
    return lines, new


def main() -> int:
    status = vex_status()
    new = 0
    for arg in sys.argv[1:]:
        path = Path(arg)
        image = path.name.removesuffix(".grype.json")
        lines, n = report(image,
                          json.loads(path.read_text())["matches"], status)
        new += n
        print("\n".join(lines))
    print(f"{new} finding(s) not in lib/vex.json.")
    return 1 if new else 0


if __name__ == "__main__":
    sys.exit(main())
