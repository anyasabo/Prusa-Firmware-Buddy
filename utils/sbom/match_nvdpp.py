#!/usr/bin/env python3
"""Match a CycloneDX SBOM against VulnCheck NVD++ (NVD 2.0 + VulnCheck CPE data).

Grype matches only NVD's own CPE configurations, so it misses CVEs that NVD
has not analysed yet ("Received", "Awaiting Analysis", "Deferred"). VulnCheck's
free NVD++ export adds `vcConfigurations` for many of those. This script
matches every CPE of every SBOM component against both.

    python utils/sbom/match_nvdpp.py buddy.cdx.json /path/to/nist-nvd2.zip

The export comes from the VulnCheck community API (free, needs a token):
    GET https://api.vulncheck.com/v3/backup/nist-nvd2  ->  data[0].url

Data: NVD++ by VulnCheck (https://vulncheck.com/nvd2), attribution required.

Matching is deliberately loose: any vulnerable cpeMatch for the component's
vendor:product (or an alias) whose version or range contains the component
version counts, ignoring AND-nodes with platform CPEs. Expect some false
positives; that is what VEX is for.
"""
from __future__ import annotations

import argparse
import gzip
import json
import re
import sys
import zipfile
from collections import defaultdict
from collections.abc import Iterator
from pathlib import Path
from typing import Any

# NVD files some products under several vendors, and VulnCheck sometimes uses
# yet another. Each set is treated as one product.
ALIASES = [
    {("arm", "mbed_tls"), ("trustedfirmware", "mbed_tls")},
    {("lwip_project", "lwip"), ("lwip", "lwip")},
]


def canonical(vendor: str, product: str) -> tuple[str, str]:
    key = (vendor.lower(), product.lower())
    for group in ALIASES:
        if key in group:
            return min(group)
    return key


VersionKey = tuple[tuple[int, int, str], ...]


def version_key(v: str) -> VersionKey:
    """Natural sort key: '2.28.10' > '2.28.9', 'r0.14b' > 'r0.14a' > 'r0.14'.

    Each part is (0, number, "") or (1, 0, letters), so numbers and letters
    never compare with each other.
    """
    parts = re.findall(r"\d+|[a-z]+", v.lower())
    return tuple((0, int(p), "") if p.isdigit() else (1, 0, p) for p in parts)


def cmp(a: str, b: str) -> int:
    ka, kb = version_key(a), version_key(b)
    return (ka > kb) - (ka < kb)


def in_range(version: str, m: dict[str, Any]) -> bool:
    crit = m["criteria"].split(":")
    exact = crit[5]
    if exact not in ("*", "-", ""):
        return cmp(version, exact) == 0
    if "versionStartIncluding" in m and cmp(version,
                                            m["versionStartIncluding"]) < 0:
        return False
    if "versionStartExcluding" in m and cmp(version,
                                            m["versionStartExcluding"]) <= 0:
        return False
    if "versionEndIncluding" in m and cmp(version,
                                          m["versionEndIncluding"]) > 0:
        return False
    # Within the range, or a bare "*"/"-" with no range ("all versions").
    return not ("versionEndExcluding" in m
                and cmp(version, m["versionEndExcluding"]) >= 0)


def cpe_matches(configurations: list[dict[str, Any]] | None) -> list[Any]:
    return [
        m for config in configurations or []
        for node in config.get("nodes", []) for m in node.get("cpeMatch", [])
        if m.get("vulnerable")
    ]


def sbom_targets(
        sbom: dict[str, Any]) -> list[tuple[str, str, tuple[str, str], str]]:
    """(bom-ref, name, canonical vendor:product, version) for every CPE."""
    targets = []
    for comp in sbom.get("components", []):
        cpes = [comp["cpe"]] if comp.get("cpe") else []
        cpes += [
            p["value"] for p in comp.get("properties", [])
            if p.get("name") == "syft:cpe23"
        ]
        for cpe in cpes:
            parts = cpe.split(":")
            targets.append(
                (comp["bom-ref"], comp["name"], canonical(parts[3],
                                                          parts[4]), parts[5]))
    return targets


def iter_cves(export: Path) -> Iterator[dict[str, Any]]:
    """Every CVE record in a nist-nvd2 backup zip (gzipped NVD 2.0 JSON files)."""
    with zipfile.ZipFile(export) as z:
        for name in sorted(z.namelist()):
            if not name.endswith(".json.gz"):
                continue
            with z.open(name) as raw, gzip.open(raw) as f:
                data = json.load(f)
            for item in data["vulnerabilities"]:
                yield item["cve"]


Hit = dict[str, Any]


def match(sbom: dict[str, Any], export: Path) -> dict[str, dict[str, Hit]]:
    by_product: dict[tuple[str, str], list[tuple[str, str,
                                                 str]]] = defaultdict(list)
    for ref, name, prod, version in sbom_targets(sbom):
        by_product[prod].append((ref, name, version))

    results: dict[str, dict[str, Hit]] = defaultdict(dict)
    for cve in iter_cves(export):
        for source, configs in (("nvd", cve.get("configurations")),
                                ("vulncheck", cve.get("vcConfigurations"))):
            for m in cpe_matches(configs):
                parts = m["criteria"].split(":")
                prod = canonical(parts[3], parts[4])
                for ref, name, version in by_product.get(prod, []):
                    if in_range(version, m):
                        hit = results[ref].setdefault(
                            cve["id"], {
                                "component": name,
                                "status": cve.get("vulnStatus"),
                                "sources": set(),
                            })
                        hit["sources"].add(source)
    return results


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("sbom", type=Path)
    parser.add_argument("export", type=Path, help="nist-nvd2 backup zip")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    results = match(json.loads(args.sbom.read_text()), args.export)
    rows = [{
        "ref": ref,
        "cve": cve,
        **hit, "sources": sorted(hit["sources"])
    } for ref, hits in sorted(results.items())
            for cve, hit in sorted(hits.items())]
    if args.json:
        json.dump(rows, sys.stdout, indent=1)
        print()
    else:
        for r in rows:
            print(f"{r['component']:20} {r['cve']:18} {r['status'] or '':18} "
                  f"{'+'.join(r['sources'])}")
        print(
            f"\n{len(rows)} matches. Data: NVD++ by VulnCheck "
            "(https://vulncheck.com/nvd2)",
            file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
