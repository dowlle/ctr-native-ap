"""Exercise the real no-argument native entry point with a harmless supervisor stub."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="ctr-bootstrap-") as directory:
    root = Path(directory)
    binary = root / Path(sys.argv[1]).name
    shutil.copy2(sys.argv[1], binary)
    (root / "tools").mkdir()
    receipt = root / "bootstrap.json"
    (root / "tools" / "run_editor_project.py").write_text(
        "import json,sys\nfrom pathlib import Path\n"
        "Path(__file__).resolve().parents[1].joinpath('bootstrap.json').write_text(json.dumps(sys.argv))\n",
        encoding="utf-8")
    result = subprocess.run([str(binary)], cwd=root.parent, timeout=20, check=True)
    args = json.loads(receipt.read_text())
    assert Path(args[0]) == root / "tools" / "run_editor_project.py", args
    assert args[1:] == ["--binary", str(binary)], args
    print("No-options native bootstrap PASS: adjacent supervisor, exact executable, independent of cwd; no game launched")
