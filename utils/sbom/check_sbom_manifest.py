#!/usr/bin/env python3
"""Check lib/sbom.yml, the manifest of vendored third-party code.

Fails when:
- the manifest does not validate against lib/sbom.schema.json,
- an entry's path (a directory, or a file for single-file libraries) does not
  exist,
- a git-subrepo directory (one with a .gitrepo file) has no entry, or its
  .gitrepo `commit` differs from the entry's `subrepo_commit`. That means the
  library was pulled or pushed without reviewing its version, upstream_commit,
  cpe and purl,
- an entry has a `subrepo_commit` but its directory has no .gitrepo,
- a dependency that utils/bootstrap.py downloads (toolchain, SDKs, prebuilt
  bootloaders and firmware) has no `bootstrap:<name>` entry, the entry's
  version differs from the one bootstrap.py pins, or a submodule entry's
  upstream_commit is not the commit bootstrap.py fetches.

Run from anywhere inside the repository. Needs pyyaml and jsonschema.
"""
from __future__ import annotations

import ast
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any

import jsonschema
import yaml

MANIFEST = Path("lib/sbom.yml")
SCHEMA = Path("lib/sbom.schema.json")
BOOTSTRAP = Path("utils/bootstrap.py")
BOOTSTRAP_PREFIX = "bootstrap:"


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


def bootstrap_dependencies(root: Path) -> dict[str, Any]:
    """The `dependencies` dict literal from utils/bootstrap.py, without running it."""
    tree = ast.parse((root / BOOTSTRAP).read_text())
    for node in tree.body:
        if (isinstance(node, ast.Assign) and len(node.targets) == 1
                and getattr(node.targets[0], "id", None) == "dependencies"):
            deps: dict[str, Any] = ast.literal_eval(node.value)
            return deps
    raise RuntimeError(f"{BOOTSTRAP}: no `dependencies = {{...}}` literal")


def check_bootstrap(root: Path, components: dict[str, Any]) -> list[str]:
    """Every bootstrap.py dependency is described, at the version it pins."""
    errors: list[str] = []
    deps = bootstrap_dependencies(root)
    covered: set[str] = set()
    for key, entry in components.items():
        info = entry.get("bootstrap")
        if key.startswith(BOOTSTRAP_PREFIX) != (info is not None):
            errors.append(f"{MANIFEST}: {key}: `bootstrap:` keys and the "
                          f"`bootstrap` field go together")
            continue
        if info is None:
            continue
        name = info["dependency"]
        dep = deps.get(name)
        if dep is None:
            errors.append(f"{MANIFEST}: {key}: {name} is not in {BOOTSTRAP}")
            continue
        covered.add(name)
        if info["version"] != dep.get("version"):
            errors.append(
                f"{MANIFEST}: {key}: {BOOTSTRAP} pins {name} "
                f"{dep.get('version')}, entry says {info['version']}; review "
                f"the entry (version, upstream_commit, cpe) and update it")
        sub = info.get("submodule")
        if sub is not None:
            url = dep.get("submodules", {}).get(sub)
            if url is None:
                errors.append(f"{MANIFEST}: {key}: {name} has no submodule "
                              f"{sub} in {BOOTSTRAP}")
            elif (commit :=
                  entry.get("upstream_commit")) and commit not in url:
                errors.append(f"{MANIFEST}: {key}: {BOOTSTRAP} fetches {url}, "
                              f"not upstream_commit {commit}")
    errors += [
        f"{BOOTSTRAP}: dependency {name} has no `{BOOTSTRAP_PREFIX}{name}` "
        f"entry in {MANIFEST} (use scope: tool if it is not shipped)"
        for name in sorted(set(deps) - covered)
    ]
    return errors


def check(root: Path) -> list[str]:
    errors: list[str] = []
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
        if path.startswith(BOOTSTRAP_PREFIX):
            continue
        if not (root / path).exists():
            errors.append(f"{MANIFEST}: {path}: path does not exist")

    errors += check_bootstrap(root, components)

    subrepos = subrepo_commits(root)
    for path, commit in sorted(subrepos.items()):
        entry = components.get(path)
        if entry is None:
            errors.append(
                f"{path}/.gitrepo: no entry in {MANIFEST}; add one (name, "
                f"version, repository, upstream_commit, subrepo_commit, cpe, "
                f"purl, supplier, license)")
        elif entry.get("subrepo_commit") != commit:
            errors.append(
                f"{path}/.gitrepo: commit {commit[:12]} differs from "
                f"subrepo_commit {str(entry.get('subrepo_commit'))[:12]} in "
                f"{MANIFEST}; check version, upstream_commit, cpe, purl and "
                f"patches, then update subrepo_commit")

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
