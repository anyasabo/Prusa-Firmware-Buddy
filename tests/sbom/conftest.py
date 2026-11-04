import sys
from pathlib import Path

# The SBOM tools are scripts in utils/, not an installed package.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "utils" / "sbom"))
