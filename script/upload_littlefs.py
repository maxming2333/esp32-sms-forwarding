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

try:
    import minify_html
    _has_minify_html = True
except ImportError:
    print("[upload_littlefs] 📦 'minify_html' not found, installing automatically...")
    try:
        subprocess.check_call([sys.executable, "-m", "pip", "install", "minify_html"])
        import minify_html
        _has_minify_html = True
        print("[upload_littlefs] ✅ 'minify_html' installed successfully.")
    except Exception as exc:
        print("[upload_littlefs] ⚠️  failed to install 'minify_html': %s" % exc)
        print("[upload_littlefs]     HTML will not be minified. Run: pip install minify_html")
        _has_minify_html = False


def _upload_littlefs(source, target, env):
    project_dir = env.subst("$PROJECT_DIR")
    data_dir = os.path.join(project_dir, "data")

    if not os.path.isdir(data_dir):
        print("[upload_littlefs] ⚠️  data/ directory not found, skipping filesystem upload")
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

        # Minify HTML (including inline JS/CSS) before compression.
        data_to_compress = content
        if fname.endswith(".html") and _has_minify_html:
            try:
                minified = minify_html.minify(
                    content.decode("utf-8"),
                    minify_js=True,
                    minify_css=True,
                    remove_processing_instructions=True,
                ).encode("utf-8")
                print(
                    "[upload_littlefs] 🗜️  minified  %s  (%d -> %d bytes)"
                    % (fname, len(data_to_compress), len(minified))
                )
                data_to_compress = minified
            except Exception as exc:
                print("[upload_littlefs] ⚠️  minify failed for %s: %s" % (fname, exc))

        gz_path = fpath + ".gz"
        with gzip.open(gz_path, "wb") as gf:
            gf.write(data_to_compress)
        os.remove(fpath)
        print(
            "[upload_littlefs] 📦 gzipped  %s -> %s.gz  (%d -> %d bytes)"
            % (fname, fname, len(data_to_compress), os.path.getsize(gz_path))
        )

    # Step 2: Upload LittleFS (only .gz files are present now).
    print("\n[upload_littlefs] 🚀 uploading filesystem image...")
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
        print("[upload_littlefs] 🔄 original files restored")

    if ret == 0:
        print("[upload_littlefs] ✅ filesystem upload OK")
    else:
        print("[upload_littlefs] ⚠️  filesystem upload failed (code %d)" % ret)


env.AddPostAction("upload", _upload_littlefs)  # noqa: F821
