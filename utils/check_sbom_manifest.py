#!/usr/bin/env python3
"""Check lib/sbom.yml, the manifest of vendored third-party code.

Fails when:
- the manifest does not validate against lib/sbom.schema.json,
- a git-subrepo directory (one with a .gitrepo file) has no manifest entry,
- a .gitrepo `commit` differs from the entry's `subrepo_commit`, which means the
  library was pulled or pushed without reviewing its version, upstream_commit,
  cpe and purl,
- an entry points at a path (directory, or file for single-file
  libraries) that does not exist.

Run from anywhere inside the repository. Needs pyyaml and jsonschema.
"""
from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

import jsonschema
import yaml

MANIFEST = Path("lib/sbom.yml")
SCHEMA = Path("lib/sbom.schema.json")


def repo_root() -> Path:
    out = subprocess.run(["git", "rev-parse", "--show-toplevel"],
                         capture_output=True,
                         text=True,
                         check=True)
    return Path(out.stdout.strip())


def subrepo_commits(root: Path) -> dict[str, str]:
    """Map each tracked git-subrepo directory to the commit in its .gitrepo."""
    files = subprocess.run(["git", "ls-files", "-z", "--", "*.gitrepo"],
                           cwd=root,
                           capture_output=True,
                           text=True,
                           check=True).stdout.split("\0")
    result = {}
    for name in filter(None, files):
        text = (root / name).read_text()
        match = re.search(r"^\s*commit\s*=\s*([0-9a-f]{40})\s*$", text,
                          re.MULTILINE)
        result[str(Path(name).parent)] = match.group(1) if match else ""
    return result


def check(root: Path) -> list[str]:
    errors = []
    try:
        manifest = yaml.safe_load((root / MANIFEST).read_text())
    except (OSError, yaml.YAMLError) as e:
        return [f"{MANIFEST}: cannot read: {e}"]
    schema = json.loads((root / SCHEMA).read_text())

    validator = jsonschema.Draft202012Validator(schema)
    for error in sorted(validator.iter_errors(manifest), key=str):
        where = "/".join(str(p) for p in error.absolute_path) or "(root)"
        errors.append(f"{MANIFEST}: {where}: {error.message}")
    if errors:
        return errors

    components = manifest["components"]
    for path in components:
        if not (root / path).exists():
            errors.append(f"{MANIFEST}: {path}: path does not exist")

    for path, commit in sorted(subrepo_commits(root).items()):
        entry = components.get(path)
        if entry is None:
            errors.append(
                f"{path}/.gitrepo: no entry in {MANIFEST}; add one (name, "
                f"version, upstream, upstream_commit, subrepo_commit, cpe, "
                f"purl, supplier, license)")
        elif entry.get("subrepo_commit") != commit:
            errors.append(
                f"{path}/.gitrepo: commit {commit[:12]} differs from "
                f"subrepo_commit {str(entry.get('subrepo_commit'))[:12]} in "
                f"{MANIFEST}; check version, upstream_commit, cpe, purl and "
                f"patches, then update subrepo_commit")

    subrepos = set(subrepo_commits(root))
    for path, entry in components.items():
        if "subrepo_commit" in entry and path not in subrepos:
            errors.append(
                f"{MANIFEST}: {path}: has subrepo_commit but no .gitrepo")
    return errors


def main() -> int:
    errors = check(repo_root())
    for error in errors:
        print(error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
