"""Compile and run the actual ESP-IDF replay identity core natively.

Run from any directory:
  python firmware/espidf_security/tools/run_replay_identity_tests.py
Requires gcc on PATH. No board or ESP-IDF needed.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = shutil.which("gcc")
if not compiler:
    raise SystemExit("GCC not found on PATH. Use the MinGW GCC environment.")

with tempfile.TemporaryDirectory(prefix="icvsp-replay-") as d:
    output = Path(d) / ("replay_identity_test.exe" if os.name == "nt" else "replay_identity_test")
    subprocess.run([
        compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "main"), str(ROOT / "main" / "replay_identity.c"),
        str(ROOT / "tools" / "test_replay_identity.c"), "-o", str(output)
    ], check=True)
    subprocess.run([str(output)], check=True)
