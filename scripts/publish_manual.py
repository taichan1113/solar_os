#!/usr/bin/env python3
"""Prepare/verify signed schema-v2 Docs artifacts locally. Does not upload."""
from __future__ import annotations
import argparse
import base64
import hashlib
import io
import json
import os
import re
import subprocess
import tempfile
import zipfile
from pathlib import Path
import generate_manual

CATALOG_MAX = 512 * 1024
ARCHIVE_MAX = 2 * 1024 * 1024
PAGE_MAX = 128 * 1024
ROOT = Path(__file__).resolve().parents[1]


def openssl(*args, data=None):
    result = subprocess.run(["openssl", *map(str, args)], input=data,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        # Never print private-key bytes or passphrase environment values.
        raise ValueError("OpenSSL operation failed; check key type, signature and key access")
    return result.stdout


def check_public_key(public_key):
    text = openssl("pkey", "-pubin", "-in", public_key, "-text", "-noout")
    if b"prime256v1" not in text:
        raise ValueError("Manual signing requires an ECDSA P-256 public key")


def sign_catalog(catalog: bytes, private_key: Path) -> bytes:
    # OpenSSL can obtain encrypted-key credentials from its normal environment/
    # agent integration; no passphrase argument is logged by this script.
    passin = "env:SOLAR_OS_DOCS_SIGNING_PASSPHRASE" if "SOLAR_OS_DOCS_SIGNING_PASSPHRASE" in os.environ else "pass:"
    return base64.b64encode(openssl("dgst", "-sha256", "-sign", private_key,
                                   "-passin", passin, data=catalog)) + b"\n"


def verify(directory: Path, public_key: Path, identity: str):
    check_public_key(public_key)
    doc = directory / "doc"
    raw = (doc / "catalog.json").read_bytes()
    if not 0 < len(raw) <= CATALOG_MAX:
        raise ValueError("catalog size limit")
    sig = (doc / "catalog.sig").read_bytes()
    if not 0 < len(sig) <= 512:
        raise ValueError("signature size limit")
    der = base64.b64decode(sig.strip(), validate=True)
    with tempfile.TemporaryDirectory() as tmp:
        signature = Path(tmp) / "signature.der"
        signature.write_bytes(der)
        openssl("dgst", "-sha256", "-verify", public_key, "-signature", signature, data=raw)
    catalog = json.loads(raw)
    if (catalog.get("schema") != "solaros.manual_catalog" or catalog.get("schema_version") != 2 or
            catalog.get("firmware_version") != identity or
            not re.fullmatch(r"[0-9a-f]{16}", catalog.get("revision", ""))):
        raise ValueError("catalog schema/identity/revision mismatch")
    archive_info = catalog["archive"]
    if archive_info["path"] != "manual.zip":
        raise ValueError("invalid archive path")
    archive = (doc / "manual.zip").read_bytes()
    if (not 0 < len(archive) <= ARCHIVE_MAX or len(archive) != archive_info["size"] or
            hashlib.sha256(archive).hexdigest() != archive_info["sha256"]):
        raise ValueError("archive size/hash mismatch")
    pages = catalog["pages"]
    if not 0 < len(pages) <= 256:
        raise ValueError("page count limit")
    seen = set()
    with zipfile.ZipFile(io.BytesIO(archive)) as zipped:
        names = zipped.namelist()
        if len(names) != len(set(names)) or len(names) != len(pages):
            raise ValueError("duplicate or unexpected ZIP entries")
        for page in pages:
            page_id = page["id"]
            if (not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", page_id) or
                    page_id in seen or page["path"] != f"manual/{page_id}.md" or
                    not 0 < page["size"] <= PAGE_MAX):
                raise ValueError("invalid page metadata/path/size")
            seen.add(page_id)
            entry = zipped.getinfo(page["path"])
            if entry.file_size != page["size"]:
                raise ValueError("ZIP page size mismatch")
            body = zipped.read(entry)
            if hashlib.sha256(body).hexdigest() != page["sha256"]:
                raise ValueError("page hash mismatch")
    return catalog


def build(source, packages, output, identity, private_key, public_key, test_only=False):
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,31}", identity):
        raise ValueError("invalid Manual identity")
    if output.exists():
        raise ValueError("output already exists; use a fresh release directory")
    check_public_key(public_key)
    pages = generate_manual.load_pages(source, packages)
    generate_manual.validate_release_pages(pages)
    archive = generate_manual.build_archive(pages)
    catalog = json.loads(generate_manual.render_catalog(pages, identity, archive))
    # Identity is part of the immutable revision namespace, unlike legacy
    # generator output. Never reuse a revision for another Manual target.
    catalog["revision"] = hashlib.sha256(
        (identity + ":" + catalog["revision"]).encode("ascii")).hexdigest()[:16]
    raw = (json.dumps(catalog, separators=(",", ":"), ensure_ascii=True) + "\n").encode("ascii")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent) as tmp:
        stage = Path(tmp)
        (stage / "doc").mkdir()
        (stage / "doc/catalog.json").write_bytes(raw)
        (stage / "doc/catalog.sig").write_bytes(sign_catalog(raw, private_key))
        (stage / "doc/manual.zip").write_bytes(archive)
        if test_only:
            (stage / "TEST-ONLY.txt").write_text("HOST TEST KEY: DO NOT PUBLISH OR TRUST IN PRODUCTION\n", encoding="ascii")
        verify(stage, public_key, identity)
        # Publish locally only after every signature/hash check succeeds.
        stage.rename(output)
    return catalog


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    create = sub.add_parser("build")
    create.add_argument("--input", type=Path, default=ROOT / "doc/manual")
    create.add_argument("--packages", type=Path, default=ROOT / "packages/solar_os_packages.toml")
    create.add_argument("--private-key", type=Path, required=True)
    create.add_argument("--test-only", action="store_true")
    for p in (create, sub.add_parser("verify")):
        p.add_argument("--directory", type=Path, required=True)
        p.add_argument("--identity", required=True)
        p.add_argument("--public-key", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "build":
        result = build(args.input, args.packages, args.directory, args.identity,
                       args.private_key, args.public_key, args.test_only)
    else:
        result = verify(args.directory, args.public_key, args.identity)
    print(json.dumps({"identity": result["firmware_version"], "revision": result["revision"],
                      "pages": len(result["pages"]), "archive_sha256": result["archive"]["sha256"]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
