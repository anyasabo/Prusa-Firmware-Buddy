"""Tests for the SBOM tooling in utils/ (run: pytest tests/sbom)."""
from __future__ import annotations

import pytest

import gen_sbom
import gen_vex
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
