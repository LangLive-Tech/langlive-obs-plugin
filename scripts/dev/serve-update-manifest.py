#!/usr/bin/env python3
"""Local update server for testing the plugin's update check.

Writes a latest.json for the given installers (computing their SHA-256) and
serves it with the installers on 127.0.0.1. Build the plugin with

    cmake --preset macos -DLANGLIVE_UPDATE_MANIFEST_URL=http://127.0.0.1:8765/latest.json

then start OBS. The plugin accepts plain HTTP only from localhost.

Examples:
    # an optional update to 0.2.0
    scripts/dev/serve-update-manifest.py --version 0.2.0 \\
        --macos release/langlive-obs-plugin-macos-universal-v0.2.0.pkg

    # a required update (the running 0.1.0 is below min_supported_version)
    scripts/dev/serve-update-manifest.py --version 0.2.0 --min-supported 0.2.0 --macos ...

    # a corrupted download: the plugin must reject it
    scripts/dev/serve-update-manifest.py --version 0.2.0 --bad-sha256 --macos ...
"""

import argparse
import datetime
import hashlib
import http.server
import json
import pathlib
import shutil
import sys
import tempfile


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", required=True, help="latest version in the manifest, e.g. 0.2.0")
    parser.add_argument("--min-supported", default="0.0.0", help="min_supported_version (default 0.0.0)")
    parser.add_argument("--macos", type=pathlib.Path, help="macOS installer (.pkg)")
    parser.add_argument("--windows", type=pathlib.Path, help="Windows installer (.exe)")
    parser.add_argument("--bad-sha256", action="store_true", help="publish a wrong SHA-256 to test verification")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()

    if not args.macos and not args.windows:
        parser.error("give at least one of --macos / --windows")

    root = pathlib.Path(tempfile.mkdtemp(prefix="langlive-update-"))
    base_url = f"http://127.0.0.1:{args.port}"
    platforms = {}
    for key, installer in (("macos", args.macos), ("windows-x64", args.windows)):
        if not installer:
            continue
        if not installer.is_file():
            parser.error(f"not a file: {installer}")
        shutil.copy2(installer, root / installer.name)
        digest = "0" * 64 if args.bad_sha256 else sha256(installer)
        platforms[key] = {"url": f"{base_url}/{installer.name}", "sha256": digest}

    manifest = {
        "version": args.version,
        "released_at": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "min_supported_version": args.min_supported,
        "platforms": platforms,
    }
    (root / "latest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")

    print(f"Serving {root} at {base_url}/latest.json")
    print(json.dumps(manifest, ensure_ascii=False, indent=2))
    sys.stdout.flush()

    handler = lambda *a, **kw: http.server.SimpleHTTPRequestHandler(*a, directory=str(root), **kw)
    with http.server.ThreadingHTTPServer(("127.0.0.1", args.port), handler) as server:
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
    shutil.rmtree(root, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
