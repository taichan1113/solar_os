"""Signed release -> actual C Docs/JSON/ZIP install on virtual Host storage."""
import base64
import copy
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import generate_docs_config as config
import publish_manual as publish

IDENTITY = "4.16.1-papermono.1"


class DocsConfigurationTest(unittest.TestCase):
    def test_papermono_waits_and_other_board_retains_ota(self):
        self.assertEqual(config.configuration("m5paper_mono", "4.16.1"), (True, IDENTITY, "", ""))
        self.assertEqual(config.configuration("solar_term", "4.16.1"), (False, "4.16.1", "", ""))

    def test_https_url_and_identity_validation(self):
        for url in ("http://host/path", "https://", "https://user:secret@host", "https://host/?x=1",
                    "https://host/#fragment", "https://host/ space", "x" * 300):
            with self.subTest(url=url), self.assertRaises(ValueError):
                config.configuration("m5paper_mono", "4.16.1", url=url)
        with self.assertRaises(ValueError):
            config.configuration("m5paper_mono", "4.16.1", manual_id="../wrong")


class SignedManualEndToEndTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which("gcc") or not shutil.which("openssl"):
            raise unittest.SkipTest("Linux gcc/OpenSSL Host test environment required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="solar-docs-")
        cls.work = Path(cls.tmp.name)
        cls.private = cls.work / "test-private.pem"
        cls.public = cls.work / "test-public.pem"
        publish.openssl("genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256", "-out", cls.private)
        publish.openssl("pkey", "-in", cls.private, "-pubout", "-out", cls.public)
        cls.release = cls.work / "release"
        cls.catalog = publish.build(ROOT / "doc/manual", ROOT / "packages/solar_os_packages.toml",
                                    cls.release, IDENTITY, cls.private, cls.public, test_only=True)
        cls.binaries = {}
        cjson = Path(os.environ.get("CJSON_DIR", "/mnt/c/Users/takec/.platformio/packages/framework-espidf/components/json/cJSON"))
        idf = cjson.parents[2]
        for flavor in ("dedicated", "direct", "waiting", "legacy"):
            generated = cls.work / flavor
            generated.mkdir()
            settings = {"url": "https://docs.invalid/releases/latest/", "public_key_file": str(cls.public)} if flavor in ("dedicated", "direct") else {}
            if flavor == "direct": settings["url"] = f"https://docs.invalid/releases/{IDENTITY}/"
            (generated / "solar_os_docs_build.h").write_text(config.render(
                "solar_term" if flavor == "legacy" else "m5paper_mono", "4.16.1", **settings))
            binary = generated / "docs-test"
            command = ["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-D_GNU_SOURCE",
                       "-include", str(ROOT / "tests/host/compat.h"), "-ffunction-sections", "-fdata-sections"]
            for inc in (generated, ROOT / "tests/host/production_stubs", ROOT / "tests/host/model_bundle_stubs",
                        ROOT / "tests/host", ROOT / "include", ROOT / "src", ROOT / "src/services", cjson):
                command += ["-I", str(inc)]
            for inc in (idf / "components/esp_rom/include", idf / "components/esp_rom/esp32s3"):
                command += ["-isystem", str(inc)]
            command += [str(ROOT / "tests/host/docs_distribution_test.c"), str(ROOT / "src/services/solar_os_json.c"),
                        str(ROOT / "src/services/solar_os_zip.c"), str(cjson / "cJSON.c"),
                        "-Wl,--gc-sections,--wrap=fwrite", "-lcrypto", "-lz", "-o", str(binary)]
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(result.stderr)
            cls.binaries[flavor] = binary

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()  # Removes temporary private keys; none enter Git/artifacts.

    def setUp(self):
        self.case = tempfile.TemporaryDirectory(dir=self.work)
        # Device mount is /sdcard. A long nested Host temp prefix would consume
        # the real firmware's bounded path budget before a page name is added.
        self.sd = tempfile.TemporaryDirectory(prefix="d")
        self.root = Path(self.sd.name)
        self.server = Path(self.case.name) / "server"
        shutil.copytree(self.release, self.server)

    def tearDown(self):
        self.sd.cleanup()
        self.case.cleanup()

    def run_c(self, mode="install", flavor="dedicated"):
        result = subprocess.run([self.binaries[flavor], self.root, self.server, mode], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def write_catalog(self, catalog, key=None):
        raw = (json.dumps(catalog, separators=(",", ":")) + "\n").encode()
        (self.server / "doc/catalog.json").write_bytes(raw)
        (self.server / "doc/catalog.sig").write_bytes(publish.sign_catalog(raw, key or self.private))

    def old_installed(self):
        result = self.run_c()
        self.assertEqual(result["error"], 0, result)
        self.assertTrue(result["active"])
        self.assertEqual(result["ota_reads"], 0)
        self.assertEqual(result["requests"], 3)
        self.assertGreater(result["bytes"], 0)
        self.assertLess(result["pages"], len(self.catalog["pages"]))  # Package gates applied.
        return result["revision"]

    def rejected_retains(self, revision, mode="install"):
        result = self.run_c(mode)
        self.assertNotEqual(result["error"], 0)
        if mode == "disconnect":
            self.assertFalse(result["active"])
            self.assertEqual((self.root / ".solar/docs/active").read_text().strip(), revision)
            offline = self.run_c("offline")  # Reinserted virtual storage.
            self.assertEqual(offline["revision"], revision)
            self.assertEqual(offline["read"], 0)
        else:
            self.assertTrue(result["active"])
            self.assertEqual(result["revision"], revision)
            self.assertEqual(result["read"], 0)
        self.assertEqual(result["ota_reads"], 0)

    def new_revision(self):
        catalog = copy.deepcopy(self.catalog)
        catalog["revision"] = "fedcba9876543210"
        return catalog

    def test_generate_sign_install_reactivate_and_offline(self):
        publish.verify(self.server, self.public, IDENTITY)
        old = self.old_installed()
        catalog = self.new_revision()
        self.write_catalog(catalog)
        new = self.run_c()
        self.assertEqual(new["error"], 0)
        self.assertNotEqual(old, new["revision"])
        self.assertTrue((self.root / ".solar/docs" / old / "manual/overview.md").exists())
        offline = self.run_c("offline")
        self.assertEqual(offline["requests"], 0)
        self.assertEqual(offline["read"], 0)
        self.assertEqual(offline["revision"], new["revision"])

    def test_catalog_tampering_and_signature_mismatch(self):
        old = self.old_installed()
        path = self.server / "doc/catalog.json"
        path.write_bytes(path.read_bytes() + b" ")  # Same JSON meaning, different signed bytes.
        self.rejected_retains(old)
        shutil.copyfile(self.release / "doc/catalog.json", path)
        (self.server / "doc/catalog.sig").write_bytes(base64.b64encode(b"wrong DER"))
        self.rejected_retains(old)

    def test_wrong_public_key(self):
        old = self.old_installed()
        other = Path(self.case.name) / "other-private.pem"
        publish.openssl("genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256", "-out", other)
        self.write_catalog(self.new_revision(), other)
        self.rejected_retains(old)

    def test_firmware_identity_and_offline_cache_identity(self):
        old = self.old_installed()
        catalog = self.new_revision(); catalog["firmware_version"] = "4.16.1"
        self.write_catalog(catalog); self.rejected_retains(old)
        # A signed upstream-version catalog is also rejected at offline startup.
        revision = self.root / ".solar/docs" / old
        catalog["revision"] = old
        self.write_catalog(catalog)
        for name in ("catalog.json", "catalog.sig"):
            shutil.copyfile(self.server / "doc" / name, revision / name)
        result = self.run_c("offline")
        self.assertFalse(result["active"])

    def test_archive_tampering(self):
        old = self.old_installed()
        path = self.server / "doc/manual.zip"
        data = bytearray(path.read_bytes()); data[-1] ^= 1; path.write_bytes(data)
        self.rejected_retains(old)
        with self.assertRaises(ValueError): publish.verify(self.server, self.public, IDENTITY)

    def test_corrupted_extracted_page_and_storage_disconnect(self):
        old = self.old_installed()
        self.write_catalog(self.new_revision())
        self.rejected_retains(old, "page-corrupt")
        self.rejected_retains(old, "disconnect")

    def test_bad_relative_paths(self):
        old = self.old_installed()
        catalog = self.new_revision(); catalog["pages"][0]["path"] = "../escape.md"
        self.write_catalog(catalog); self.rejected_retains(old)
        # Signed archive with an unexpected traversal entry: real ZIP parser rejects.
        catalog = self.new_revision()
        out = io.BytesIO()
        with zipfile.ZipFile(out, "w") as z:
            z.writestr("../escape.md", "bad")
        data = out.getvalue(); (self.server / "doc/manual.zip").write_bytes(data)
        catalog["archive"].update(size=len(data), sha256=hashlib.sha256(data).hexdigest())
        self.write_catalog(catalog); self.rejected_retains(old)
        self.assertFalse((self.root / ".solar/docs/escape.md").exists())

    def test_interrupted_download(self):
        old = self.old_installed(); self.write_catalog(self.new_revision())
        self.rejected_retains(old, "interrupt")

    def test_empty_corrupt_and_oversized_catalog(self):
        old = self.old_installed()
        for raw in (b"", b"not JSON", b"x" * (publish.CATALOG_MAX + 1)):
            (self.server / "doc/catalog.json").write_bytes(raw)
            (self.server / "doc/catalog.sig").write_bytes(publish.sign_catalog(raw, self.private))
            self.rejected_retains(old)

    def test_signed_size_limits(self):
        old = self.old_installed()
        for target, size in (("archive", publish.ARCHIVE_MAX + 1), ("page", publish.PAGE_MAX + 1)):
            catalog = self.new_revision()
            (catalog["archive"] if target == "archive" else catalog["pages"][0])["size"] = size
            self.write_catalog(catalog); self.rejected_retains(old)

    def test_configuration_and_ota_independence(self):
        result = self.run_c("config", "waiting")
        self.assertNotEqual(result["config"], 0)
        self.assertEqual(result["ota_reads"], 0)
        direct = self.run_c("config", "direct")
        self.assertEqual(direct["url"], f"https://docs.invalid/releases/{IDENTITY}")
        waiting = self.run_c("install", "waiting")
        self.assertEqual(waiting["requests"], 0)
        self.assertFalse(waiting["active"])
        result = self.run_c("config", "dedicated")
        self.assertEqual(result["url"], f"https://docs.invalid/releases/{IDENTITY}")
        self.assertEqual(result["ota_reads"], 0)
        result = self.run_c("config", "legacy")
        self.assertEqual(result["url"], "https://ota.invalid/releases/4.16.1")
        self.assertEqual(result["ota_reads"], 1)

    def test_public_key_guard_and_build_cache_fingerprint(self):
        with self.assertRaises(ValueError):
            config.configuration("m5paper_mono", "4.16.1", public_key_file=str(self.private))
        invalid = Path(self.case.name) / "bad-public.pem"
        invalid.write_text("-----BEGIN PUBLIC KEY-----\nYWJj\n-----END PUBLIC KEY-----\n")
        with self.assertRaises(ValueError):
            config.configuration("m5paper_mono", "4.16.1", public_key_file=str(invalid))
        with patch.dict(os.environ, {"SOLAR_OS_DOCS_RELEASE_URL": "", "SOLAR_OS_DOCS_PUBLIC_KEY_FILE": "", "SOLAR_OS_DOCS_FIRMWARE_ID": ""}):
            before = config.fingerprint("m5paper_mono", "4.16.1")
            os.environ["SOLAR_OS_DOCS_PUBLIC_KEY_FILE"] = str(self.public)
            after = config.fingerprint("m5paper_mono", "4.16.1")
            self.assertNotEqual(before, after)
            os.environ["SOLAR_OS_DOCS_RELEASE_URL"] = "https://docs.invalid/releases/latest"
            self.assertNotEqual(after, config.fingerprint("m5paper_mono", "4.16.1"))


if __name__ == "__main__":
    unittest.main()
