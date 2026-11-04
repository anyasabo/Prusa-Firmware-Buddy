"""Shared access to lib/sbom.yml for the SBOM tools."""
from __future__ import annotations

from pathlib import Path
from typing import Any

import yaml

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "lib" / "sbom.yml"

Entry = dict[str, Any]


def load_components() -> dict[str, Entry]:
    """Manifest entries keyed by path (or `bootstrap:<dependency>`)."""
    return yaml.safe_load(MANIFEST.read_text())["components"]


def scope(entry: Entry) -> str:
    """`shipped`, `test` or `tool`; entries without a scope are shipped."""
    return entry.get("scope", "shipped")
