"""
Post-upload script: gzip-compress files in data/, upload LittleFS filesystem
image, then restore the original uncompressed files.

ESPAsyncWebServer's serveStatic() probes <file>.gz before <file>, so uploading
only the .gz versions saves flash space while keeping development files readable.

Triggered by: pio run -t upload  (or clicking "Upload" in IDE)
"""

Import("env")  # noqa: F821 — SCons injects this

import gzip
import os
import subprocess
import sys


def _upload_littlefs(source, target, env):
    project_dir = env.subst("$PROJECT_DIR")
    data_dir = os.path.join(project_dir, "data")

    if not os.path.isdir(data_dir):
        print("[LittleFS] data/ directory not found, skipping filesystem upload")
        return

    # Step 1: Compress every non-gz file in data/ and remove the original.
    #         Keep the raw bytes in memory so we can restore them afterward.
    originals = {}  # fname -> bytes
    for fname in os.listdir(data_dir):
        fpath = os.path.join(data_dir, fname)
        if not os.path.isfile(fpath) or fname.endswith(".gz"):
            continue
        with open(fpath, "rb") as f:
            content = f.read()
        originals[fname] = content
        gz_path = fpath + ".gz"
        with gzip.open(gz_path, "wb") as gf:
            gf.write(content)
        os.remove(fpath)
        print(
            "[LittleFS] Compressed %s -> %s.gz  (%d -> %d bytes)"
            % (fname, fname, len(content), os.path.getsize(gz_path))
        )

    # Step 2: Upload LittleFS (only .gz files are present now).
    print("\n[LittleFS] Uploading filesystem image...")
    ret = 1
    try:
        ret = subprocess.call(
            [
                sys.executable, "-m", "platformio",
                "run",
                "--target", "uploadfs",
                "--environment", env["PIOENV"],
            ],
            cwd=project_dir,
        )
    finally:
        # Step 3: Remove generated .gz files and restore originals.
        for fname, content in originals.items():
            fpath = os.path.join(data_dir, fname)
            gz_path = fpath + ".gz"
            if os.path.exists(gz_path):
                os.remove(gz_path)
            with open(fpath, "wb") as f:
                f.write(content)
        print("[LittleFS] Original files restored")

    if ret == 0:
        print("[LittleFS] Filesystem upload OK")
    else:
        print("[LittleFS] WARNING: filesystem upload failed (code %d)" % ret)


env.AddPostAction("upload", _upload_littlefs)  # noqa: F821
