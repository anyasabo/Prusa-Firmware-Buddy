#!/usr/bin/env python3
"""Vulnerability sources that CPE scanners (Grype, OSV) miss, for lib/sbom.yml.

Fork-only tooling for the nightly scan; not meant for upstream. Subcommands:

  mbedtls-advisories DOCS_DIR
      Parse the "Affects" line of every advisory in a checkout of
      https://github.com/Mbed-TLS/mbedtls-docs (security-advisories/) and list
      the ones that cover an mbedTLS version in the manifest. Upstream
      publishes advisories before (sometimes months before) any CVE record.
  gh-advisories
      Published GitHub repository security advisories of every GitHub
      upstream in the manifest (uses GITHUB_TOKEN if set).
  nvdpp-recent EXPORT_ZIP [--days N]
      CVEs published in the last N days whose description names a manifest
      component (VulnCheck NVD++ export; attribution: NVD++ by VulnCheck).
      Catches CVEs that have no CPE data yet.
  kev CVE...
      Which of the given CVEs are in VulnCheck KEV (needs
      CLAUDE_VULNCHECK_TOKEN or VULNCHECK_TOKEN).
  espressif SDK_CLONE
      For each Espressif Wi-Fi advisory fix commit below, whether the pinned
      ESP8266 RTOS SDK commit contains it (git ancestry).

Everything already decided in lib/vex.json is reported as "known".
"""
from __future__ import annotations

import argparse
import datetime
import gzip
import io
import json
import os
import re
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "lib" / "sbom.yml"
VEX = ROOT / "lib" / "vex.json"

# Espressif advisories with per-branch fix commits (ESP8266 RTOS SDK).
# https://www.espressif.com/en/support/documents/advisories
ESPRESSIF_FIXES = [
    ("AR2020-002", "CVE-2020-12638", {
        "master": "da3362ec",
        "release/v3.4": "da3362ec"
    }),
    ("AR2021-003",
     "CVE-2020-24586,CVE-2020-24587,CVE-2020-24588,CVE-2020-26146,CVE-2020-26147",
     {
         "master": "08e225dd",
         "release/v3.4": "967752e2"
     }),
    ("AR2023-008", "CVE-2020-26142", {
        "master": "816bf9bc",
        "release/v3.4": "d47d0f4d"
    }),
]

# Description keywords per component name. Default: the component name, if it
# is distinctive enough. Generic names need explicit keywords or are skipped.
KEYWORDS = {
    "mbedtls": [r"mbed ?tls"],
    "mbedtls-espressif-fork": [],  # covered by "mbedtls"
    "lwip": [r"\blwip\b"],
    "esp-lwip": [],
    "lwip-mdns": [],
    "lwip-sntp": [],
    "freertos-kernel": [r"freertos"],
    "freertos-kernel-espressif": [],
    "fatfs": [r"\bfatfs\b", r"elm-chan"],
    "littlefs": [r"\blittlefs\b"],
    "tinyusb": [r"tinyusb"],
    "marlin": [r"marlin firmware", r"marlinfw"],
    "printf": [r"mpaland"],
    "jsmn": [r"\bjsmn\b"],
    "inih": [r"\binih\b"],
    "stm32_mw_usb_host":
    [r"usb ?host library", r"stm32_mw_usb_host", r"\bUSBH_\w+"],
    "esp8266-rtos-sdk": [r"esp8266"],
    "esp-idf": [r"esp-idf"],
    "wpa_supplicant-espressif": [r"wpa_supplicant", r"hostapd"],
    "newlib": [r"\bnewlib\b"],
    "heatshrink": [r"heatshrink"],
    "nanocbor": [r"nanocbor"],
    "libcanard": [r"libcanard"],
    "bitsery": [r"bitsery"],
    "magic_enum": [r"magic_enum"],
    "micro-ecc": [r"micro-ecc"],
}


def manifest() -> dict:
    return yaml.safe_load(MANIFEST.read_text())["components"]


def vex_known() -> dict[str, str]:
    if not VEX.exists():
        return {}
    doc = json.loads(VEX.read_text())
    return {s["vulnerability"]["name"]: s["status"] for s in doc["statements"]}


# ---------------------------------------------------------------- mbedtls ---


def vkey(v: str) -> tuple:
    return tuple(int(x) for x in re.findall(r"\d+", v)[:3])


def affects(text: str,
            version: str,
            adv_date: str | None = None,
            released: str | None = None) -> bool | None:
    """Evaluate an advisory "Affects" line for one version. None = can't tell.

    "All versions" means all versions at the time of the advisory, so it does
    not cover releases published after the advisory date.
    """
    v = vkey(version)
    # No upper bound ("all versions", "1.3.0 and up", "upwards") means
    # versions up to the advisory date; later releases contain the fix.
    plain = re.sub(r"<[^>]+>", " ", text)
    upper = re.search(r"through|\bto\b|up to|before|prior|until|<", plain,
                      re.I)
    if adv_date and released and adv_date < released and not upper:
        return False
    t = re.sub(r"<[^>]+>", " ", text).replace("`", "")
    hit = False
    understood = False
    for clause in re.split(
            r";|,(?=\s*(?:all|mbed|tf-psa|and)\b)|\band\b(?=\s*(?:all|mbed|from)\b)",
            t,
            flags=re.I):
        c = clause.strip().lower()
        if not c or "tf-psa" in c or "polarssl" in c and "mbed" not in c or "crypto" in c and "mbed tls" not in c:
            continue
        nums = re.findall(r"\d+\.\d+(?:\.\d+)?", c)
        major = re.search(
            r"mbed tls (\d+)(?:\.x)? versions|all mbed tls (\d+) ", c)
        if major and v[0] != int(major.group(1) or major.group(2)):
            understood = True
            continue
        if re.search(r"all versions(?! from| since)|all version of",
                     c) and not nums:
            hit, understood = True, True
            continue
        m = re.search(
            r"(?:from|since)?\s*(\d+\.\d+(?:\.\d+)?)\s*(?:through|to|up to(?: and including)?|-)\s*(\d+\.\d+(?:\.\d+)?)",
            c)
        if m:
            understood = True
            hit |= vkey(m.group(1)) <= v <= vkey(m.group(2))
            continue
        m = re.search(r"up to(?: and including)?\s+(\d+\.\d+(?:\.\d+)?)", c)
        if m:
            understood = True
            hit |= v <= vkey(m.group(1))
            continue
        m = re.search(
            r"(?:since|from)\s+(?:version\s+)?(\d+\.\d+(?:\.\d+)?)|(\d+\.\d+(?:\.\d+)?)\s+(?:and (?:above|later|up)|upwards)",
            c)
        if m:
            understood = True
            hit |= v >= vkey(m.group(1) or m.group(2))
            continue
        if len(nums) == 1 and re.search(r"mbed tls\s+\d", c):
            understood = True
            hit |= v == vkey(nums[0])
    return hit if understood else None


_RELEASES: dict[str, str | None] = {}


def release_date(version: str) -> str | None:
    """Upstream release date of an mbedTLS version (GitHub release), cached."""
    if version not in _RELEASES:
        _RELEASES[version] = None
        for tag in (f"v{version}", f"mbedtls-{version}"):
            try:
                _RELEASES[version] = gh_get(
                    f"/repos/Mbed-TLS/mbedtls/releases/tags/{tag}"
                )["published_at"][:10]
                break
            except Exception:
                continue
    return _RELEASES[version]


def adv_date(text: str) -> str | None:
    m = re.search(r"^\*\*Date\*\*\s*\|\s*(.+)$", text, re.M)
    if not m:
        return None
    d = m.group(1).split("(")[0].replace(" of ", " ")
    d = re.sub(r"(\d)(st|nd|rd|th)\b", r"\1", d).strip()
    for fmt in ("%d %B, %Y", "%d %B %Y", "%B %d, %Y", "%Y-%m-%d"):
        try:
            return datetime.datetime.strptime(d, fmt).strftime("%Y-%m-%d")
        except ValueError:
            pass
    return None


def cmd_mbedtls(args) -> int:
    versions = sorted({(e["name"], str(e["version"]))
                       for e in manifest().values()
                       if e["name"].startswith("mbedtls") and e.get("version")
                       })
    known = vex_known()
    adv_dir = Path(args.docs) / "security-advisories"
    for f in sorted(adv_dir.glob("mbedtls-security-advisory-*.md")):
        text = f.read_text(errors="replace")
        cve = re.search(r"^\*\*CVE\*\*\s*\|\s*(.+)$", text, re.M)
        aff = re.search(r"^\*\*Affects\*\*\s*\|\s*(.+)$", text, re.M)
        if not aff:
            continue
        cves = re.findall(r"CVE-\d+-\d+", cve.group(1)) if cve else []
        for name, ver in versions:
            r = affects(aff.group(1), ver, adv_date(text), release_date(ver))
            if r is False:
                continue
            ids = ",".join(cves) or "(no CVE)"
            state = ",".join(sorted({known.get(c, "NEW")
                                     for c in cves})) if cves else "NEW"
            print(f"{state:20} {name} {ver}  {ids:30} {f.name}" +
                  ("" if r else "  [affects line not understood]"))
    return 0


# ------------------------------------------------------------- github ------


def gh_get(path: str):
    req = urllib.request.Request(f"https://api.github.com{path}",
                                 headers={
                                     "Accept": "application/vnd.github+json",
                                     "User-Agent": "buddy-sbom-feeds"
                                 })
    tok = os.environ.get("GITHUB_TOKEN")
    if tok:
        req.add_header("Authorization", f"Bearer {tok}")
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def cmd_gh(args) -> int:
    known = vex_known()
    repos = {}
    for key, e in manifest().items():
        m = re.match(r"https?://github\.com/([^/]+)/([^/.]+)",
                     e.get("repository", ""))
        if m and e.get("scope", "shipped") == "shipped":
            repos.setdefault(f"{m.group(1)}/{m.group(2)}", []).append(e)
    for repo, entries in sorted(repos.items()):
        try:
            advs = gh_get(
                f"/repos/{repo}/security-advisories?state=published&per_page=100"
            )
        except Exception as exc:  # private, renamed, rate-limited
            print(f"{'ERROR':20} {repo}: {exc}")
            continue
        for a in advs:
            cve = a.get("cve_id") or a["ghsa_id"]
            ranges = ";".join(
                v.get("vulnerable_version_range") or ""
                for v in a.get("vulnerabilities") or [])
            pins = ",".join(sorted({str(e.get('version')) for e in entries}))
            print(
                f"{known.get(cve, 'NEW'):20} {repo} pinned={pins} {cve} [{ranges}] {a['summary'][:70]}"
            )
    return 0


# -------------------------------------------------------------- nvd++ ------


def cmd_nvdpp(args) -> int:
    known = vex_known()
    since = (datetime.datetime.now(datetime.timezone.utc) -
             datetime.timedelta(days=args.days)).strftime("%Y-%m-%d")
    pats = []
    for e in manifest().values():
        if e.get("scope", "shipped") != "shipped":
            continue
        words = KEYWORDS.get(
            e["name"], [re.escape(e["name"])] if len(e["name"]) > 5 else [])
        for w in words:
            pats.append((e["name"], re.compile(w, re.I)))
    with zipfile.ZipFile(args.export) as z:
        for name in sorted(z.namelist()):
            if not name.endswith(".json.gz"):
                continue
            for item in json.load(gzip.open(io.BytesIO(
                    z.read(name))))["vulnerabilities"]:
                c = item["cve"]
                if c.get("published",
                         "") < since or c.get("vulnStatus") == "Rejected":
                    continue
                d = next((x["value"]
                          for x in c["descriptions"] if x["lang"] == "en"), "")
                hits = sorted({n for n, p in pats if p.search(d)})
                if hits:
                    print(
                        f"{known.get(c['id'], 'NEW'):20} {c['id']:16} {c['published'][:10]} "
                        f"{','.join(hits)} | {d[:140]}")
    print("Data: NVD++ by VulnCheck (https://vulncheck.com/nvd2)",
          file=sys.stderr)
    return 0


# ---------------------------------------------------------------- kev ------


def cmd_kev(args) -> int:
    tok = os.environ.get("VULNCHECK_TOKEN") or os.environ.get(
        "CLAUDE_VULNCHECK_TOKEN")
    if not tok:
        sys.exit("kev: set VULNCHECK_TOKEN")
    for cve in args.cves:
        req = urllib.request.Request(
            f"https://api.vulncheck.com/v3/index/vulncheck-kev?cve={cve}",
            headers={"Authorization": f"Bearer {tok}"})
        with urllib.request.urlopen(req, timeout=30) as r:
            n = len(json.load(r).get("data") or [])
        if n:
            print(f"KEV {cve}")
    return 0


# ----------------------------------------------------------- espressif -----


def cmd_espressif(args) -> int:
    sdk = manifest()["bootstrap:esp8266-rtos-sdk"]["upstream_commit"]
    git = ["git", "-C", args.clone]
    for adv, cves, fixes in ESPRESSIF_FIXES:
        fixed = any(
            subprocess.run(
                git + ["merge-base", "--is-ancestor", c, sdk]).returncode == 0
            for c in fixes.values())
        print(
            f"{'fixed' if fixed else 'NOT FIXED':10} {adv} {cves} (pin {sdk[:10]})"
        )
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("mbedtls-advisories")
    s.add_argument("docs")
    s.set_defaults(f=cmd_mbedtls)
    s = sub.add_parser("gh-advisories")
    s.set_defaults(f=cmd_gh)
    s = sub.add_parser("nvdpp-recent")
    s.add_argument("export")
    s.add_argument("--days", type=int, default=30)
    s.set_defaults(f=cmd_nvdpp)
    s = sub.add_parser("kev")
    s.add_argument("cves", nargs="+")
    s.set_defaults(f=cmd_kev)
    s = sub.add_parser("espressif")
    s.add_argument("clone")
    s.set_defaults(f=cmd_espressif)
    args = p.parse_args()
    return args.f(args)


if __name__ == "__main__":
    sys.exit(main())
