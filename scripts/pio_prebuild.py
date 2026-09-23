from pathlib import Path
import sys

Import("env")

project_dir = Path(env["PROJECT_DIR"])

sys.path.insert(0, str(project_dir / "scripts"))

from generate_config import generate

generate(project_dir)
