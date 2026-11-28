"""Tests for the SBOM tooling in utils/ (run: pytest tests/sbom)."""
from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

import gen_sbom
import gen_vex
import vex_gate
from match_nvdpp import in_range, version_key
from sbom_feeds import affects
from sbom_manifest import load_components

# GitHub release dates of the mbedTLS versions Buddy ships.
RELEASED = {"2.28.0": "2021-12-17", "3.4.0": "2023-03-28"}

# Real "Affects" lines from Mbed-TLS/mbedtls-docs security-advisories/, with
# the advisory date and the expected result for 2.28.0 and 3.4.0.
ADVISORIES = [
    # No upper bound, published before both releases: fixed in them.
    ("mbed TLS 1.4 and up", "2017-03-10", False, False),
    ("All versions of Mbed TLS from version 1.2 upwards, including<br>"
     "all 2.1, 2.7 and later releases", "2018-07-25", False, False),
    ("All versions of Mbed TLS", "2025-03-24", True, True),
    ("All versions of Mbed TLS up to and including 2.28.0 and 3.1.0",
     "2022-07-11", True, False),
    ("Mbed TLS 3.2.0 and above", "2023-10-05", False, True),
    ("All versions of Mbed TLS from 2.18.0 to 2.28.7 and from 3.0.0 to 3.5.2; "
     "all versions of Mbed Crypto", "2024-03-25", True, True),
    ("All versions of Mbed TLS since 2.26.0", "2024-08-30", True, True),
    ("Mbed TLS 3.6.0", "2024-08-30", False, False),
    ("Mbed TLS 3.2.0 to 3.6.0 included", "2024-08-30", False, True),
    ("all version of Mbed TLS up to 3.6.4", "2025-10-15", True, True),
    ("Mbed TLS versions 3.5.0 up to and including 3.6.5, and 4.0.0",
     "2026-03-31", False, False),
    ("TF-PSA-Crypto 1.0 and all versions of Mbed TLS up to 3.6.5",
     "2026-03-31", True, True),
    ("All Mbed TLS versions from 2.19.0 up to 3.6.5, Mbed TLS 4.0.0",
     "2026-03-31", True, True),
    ("Mbed TLS 3.5.0 through 3.6.6, all Mbed TLS 4 versions up to 4.1.0",
     "2026-07-07", False, False),
    ("Mbed TLS 2.0.0 through 2.28.10; Mbed TLS 3.0.0 through 3.6.6; "
     "Mbed TLS 4.0.0 through 4.1.0", "2026-07-07", True, True),
    ("TF-PSA-Crypto 1.1.0, Mbed TLS 4.1.0", "2026-07-07", False, False),
    # Only other products: nothing to evaluate.
    ("All versions of Mbed TLS and TF-PSA-Crypto", "2026-03-31", None, None),
]


@pytest.mark.parametrize("text,date,on_2_28,on_3_4", ADVISORIES)
def test_affects(text: str, date: str, on_2_28: bool | None,
                 on_3_4: bool | None) -> None:
    assert affects(text, "2.28.0", date, RELEASED["2.28.0"]) is on_2_28
    assert affects(text, "3.4.0", date, RELEASED["3.4.0"]) is on_3_4


# Lines of November 2026, for the mbedtls 4.1 and TF-PSA-Crypto 1.1 releases before and with the
# fixes: (text, product, fixed version, version before the fix).
ADVISORIES_4 = [
    ("Mbed TLS through 3.6.7; Mbed TLS 4.0.0 through 4.1.1; Mbed TLS 4.2.0",
     "mbedtls", "4.1.2", "4.1.1"),
    ("All versions of Mbed TLS up to 3.6.7; all versions of TF-PSA-Crypto "
     "1.1.x up to 1.1.1; TF-PSA-Crypto 1.2.0", "tf-psa-crypto", "1.1.2",
     "1.1.1"),
    ("Mbed TLS through 3.6.7; Mbed TLS 4.0.0 through 4.1.1; Mbed TLS 4.2.0; "
     "TF-PSA-Crypto through 1.1.1; TF-PSA-Crypto 1.2.0", "tf-psa-crypto",
     "1.1.2", "1.1.1"),
]


@pytest.mark.parametrize("text,product,fixed,before", ADVISORIES_4)
def test_affects_mbedtls_4(text: str, product: str, fixed: str,
                           before: str) -> None:
    assert affects(text, fixed, product=product) is False
    assert affects(text, before, product=product) is True


def test_tf_psa_crypto_clauses_do_not_count_for_mbedtls() -> None:
    text = "All versions of TF-PSA-Crypto up to 1.1.1"
    assert affects(text, "4.1.1") is None
    assert affects(text, "1.1.1", product="tf-psa-crypto") is True
    assert affects("Mbed TLS 4.0.0 through 4.1.1",
                   "1.1.1",
                   product="tf-psa-crypto") is False


def test_version_key_orders_naturally() -> None:
    assert version_key("2.28.10") > version_key("2.28.9")
    assert version_key("r0.14b") > version_key("r0.14a") > version_key("r0.14")


def test_in_range() -> None:
    cpe = "cpe:2.3:a:arm:mbed_tls:*:*:*:*:*:*:*:*"
    m = {"criteria": cpe, "versionEndExcluding": "2.28.1"}
    assert in_range("2.28.0", m)
    assert not in_range("2.28.1", m)
    assert in_range(
        "2.28.0",
        {"criteria": cpe.replace(":*:*:*:*:*:*:*:*", ":2.28.0:*:*:*:*:*:*:*")})


def test_every_triaged_component_has_a_purl() -> None:
    statements = gen_vex.statements()  # exits on an unknown component
    assert all(s["products"][0]["@id"].startswith("pkg:") for s in statements)


def test_image_selection_defaults_to_main() -> None:
    components = load_components()
    main = {
        p
        for p, e in components.items()
        if gen_sbom.selected(e, {"shipped"}, "main")
    }
    esp = {
        p
        for p, e in components.items()
        if gen_sbom.selected(e, {"shipped"}, "esp32-wifi")
    }
    assert "lib/Middlewares/Third_Party/mbedtls" in main
    assert "bootstrap:esp-idf" in esp and "bootstrap:esp-idf" not in main


def match(cve: str, purl: str, related: tuple[str, ...] = ()) -> dict:
    return {
        "artifact": {
            "purl": purl,
            "name": "x",
            "version": "1"
        },
        "vulnerability": {
            "id": cve
        },
        "relatedVulnerabilities": [{
            "id": r
        } for r in related],
    }


def test_vex_gate_counts_only_undecided_findings() -> None:
    status = {("CVE-1", "pkg:a"): "affected"}
    lines, new = vex_gate.report("main", [
        match("CVE-1", "pkg:a"),
        match("GHSA-x", "pkg:a", related=("CVE-1", )),
        match("CVE-1", "pkg:b"),
    ], status)
    assert new == 1  # decided for pkg:a only
    assert "- **NEW**: CVE-1 in x 1 (?)" in lines


def test_vex_gate_warns_and_passes_on_new_findings(
        tmp_path, monkeypatch: pytest.MonkeyPatch,
        capsys: pytest.CaptureFixture[str]) -> None:
    # A new CVE must not fail the build: it is a warning for a person to triage.
    result = tmp_path / "main.grype.json"
    result.write_text(json.dumps({"matches": [match("CVE-NEW", "pkg:new")]}))
    monkeypatch.setenv("GITHUB_ACTIONS", "true")
    monkeypatch.setattr(sys, "argv", ["vex_gate.py", str(result)])
    assert vex_gate.main() == 0
    out, err = capsys.readouterr()
    assert "1 finding(s) not in lib/vex.json." in out
    assert "::warning title=New vulnerability::CVE-NEW in x 1" in err


def test_vex_gate_fails_on_unreadable_results(
        tmp_path, monkeypatch: pytest.MonkeyPatch) -> None:
    # A tool error (here: Grype wrote no JSON) still fails.
    result = tmp_path / "main.grype.json"
    result.write_text("")
    monkeypatch.setattr(sys, "argv", ["vex_gate.py", str(result)])
    with pytest.raises(json.JSONDecodeError):
        vex_gate.main()


def test_scan_refuses_a_grype_download_with_a_wrong_sha256(tmp_path) -> None:
    # A tool error must fail the scan, and the unverified file must not stay in the cache.
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    fake_curl = bin_dir / "curl"
    # scan.sh calls curl -fsSLo FILE URL. Give a real tarball with another grype in it.
    fake_curl.write_text(
        '#!/bin/sh\nd=$(mktemp -d)\nprintf "#!/bin/sh\\n" > "$d/grype"\n'
        'chmod +x "$d/grype"\ntar -czf "$2" -C "$d" grype\n')
    fake_curl.chmod(0o755)
    env = {
        **os.environ, "PATH": f"{bin_dir}:/usr/bin:/bin",
        "XDG_CACHE_HOME": str(tmp_path / "cache")
    }
    root = Path(__file__).resolve().parents[2]
    result = subprocess.run(
        ["bash", "utils/sbom/scan.sh",
         str(tmp_path / "out")],
        check=False,
        cwd=root,
        env=env,
        capture_output=True,
        text=True)
    assert result.returncode != 0
    assert not list((tmp_path / "cache").rglob("grype"))
