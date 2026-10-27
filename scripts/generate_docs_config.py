#!/usr/bin/env python3
"""Generate independent Docs build settings; never reads or changes OTA/NVS."""
import argparse
import base64
import hashlib
import json
import os
import re
from pathlib import Path
from urllib.parse import urlsplit


def configuration(board, version, url="", public_key_file="", manual_id=""):
    dedicated = board == "m5paper_mono" or bool(url or public_key_file or manual_id)
    edition = "papermono" if board == "m5paper_mono" else board
    identity = manual_id or (f"{version}-{edition}.1" if dedicated else version)
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,31}", identity):
        raise ValueError("Docs identity must contain 1..31 ASCII letters/digits/._-")
    if url:
        parsed = urlsplit(url)
        if (parsed.scheme != "https" or not parsed.hostname or parsed.username or
                parsed.password or parsed.query or parsed.fragment or
                any(c.isspace() or ord(c) < 32 or ord(c) > 126 for c in url) or len(url) > 220):
            raise ValueError("Docs URL must be an HTTPS release base without credentials/query/fragment")
        base = url.rstrip("/")
        if base.endswith("/latest"):
            base = base[:-7] + "/" + identity
        if len(base + "/doc/catalog.json") >= 256:
            raise ValueError("resolved Docs artifact URL exceeds firmware limit")
    key = Path(public_key_file).read_text(encoding="ascii") if public_key_file else ""
    if key and ("-----BEGIN PUBLIC KEY-----" not in key or "PRIVATE KEY" in key):
        raise ValueError("Docs key file must contain a public PEM, never a private key")
    if key:
        body = key.replace("-----BEGIN PUBLIC KEY-----", "").replace("-----END PUBLIC KEY-----", "")
        der = base64.b64decode("".join(body.split()), validate=True)
        # Canonical uncompressed P-256 SubjectPublicKeyInfo (algorithm/curve OIDs).
        prefix = bytes.fromhex("3059301306072a8648ce3d020106082a8648ce3d03010703420004")
        if len(der) != 91 or not der.startswith(prefix):
            raise ValueError("Docs key must be a P-256 public SubjectPublicKeyInfo PEM")
    return dedicated, identity, url.rstrip("/"), key


def render(board, version, **kwargs):
    dedicated, identity, url, key = configuration(board, version, **kwargs)
    return ("/* Generated Docs settings. No OTA setting is modified. */\n#pragma once\n"
            f"#define SOLAR_OS_DOCS_DEDICATED {int(dedicated)}\n"
            f"#define SOLAR_OS_DOCS_FIRMWARE_ID {json.dumps(identity)}\n"
            f"#define SOLAR_OS_DOCS_RELEASE_URL {json.dumps(url)}\n"
            f"#define SOLAR_OS_DOCS_PUBLIC_KEY_PEM {json.dumps(key)}\n")


def environment_settings():
    return dict(url=os.environ.get("SOLAR_OS_DOCS_RELEASE_URL", ""),
                public_key_file=os.environ.get("SOLAR_OS_DOCS_PUBLIC_KEY_FILE", ""),
                manual_id=os.environ.get("SOLAR_OS_DOCS_FIRMWARE_ID", ""))


def fingerprint(board, version):
    return hashlib.sha256(render(board, version, **environment_settings()).encode("utf-8")).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--board", required=True)
    p.add_argument("--version", required=True)
    p.add_argument("--output", required=True, type=Path)
    args = p.parse_args()
    result = render(args.board, args.version, **environment_settings())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != result:
        args.output.write_text(result, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
