# Unified device manual

SolarOS keeps GitHub user documentation, device help, website pages, and agent
API guidance in one canonical source: `doc/manual/*.md`. Each topic is an
ordinary Markdown guide with TOML frontmatter and a final `Quick reference`
section. `doc/manual/README.md` is a generated GitHub index over the same topic
metadata.

The build generator reads those pages and creates a package-gated C registry
of topic metadata, a small setup and recovery guide, and a shared download
notice for the other topics. Firmware does not embed the full guides or
scripting API excerpts.
`man` and text-shell `help` display a terminal-normalized form, graphic display
shells open the Markdown in `reader`, and the agent's `solaros_reference` tool
returns the downloaded `Quick reference` section, or setup instructions when
the required manual has not been installed. The website generator renders
the same files as HTML. Topic IDs, groups, aliases, summaries, keywords, and
package gates therefore cannot drift between those interfaces.

## Topic format

```markdown
+++
id = "python.gfx"
title = "Python graphics"
section = "api"
summary = "Draw safely on the active display"
aliases = ["gfx"]
keywords = "python graphics display"
packages_any = ["app_python"]
+++
# Python graphics

Tutorial text...

## Quick reference

The concise, authoritative contract used by the agent...
```

The `id` is the runtime lookup key, and the filename must be `<id>.md`. IDs and
aliases must be unique. `section` is one of the ordered documentation-tree
groups defined by the generator. `packages_any` contains package IDs from
`packages/solar_os_packages.toml`. A topic is embedded when any named package is
present; an empty list makes it universal. This gate controls the topic's
directory entry, not whether its full guide is embedded.

## Embedded fallback

`scripts/generate_manual.py` defines the setup and recovery topics. The `help`
guide explains how to connect Wi-Fi, mount storage, and install the signed
manual. A small set of command quick references covers connection, mounts,
firmware updates, diagnostics, and session control. Every other topic retains
its ID, title, aliases, summary, keywords, and package gate for browsing and
search, but its content directs the reader to `help update` or the website.

Each setup topic shares one plain-text string between the terminal and graphical
reader. The agent uses the same command references and a short setup contract
for the `help` guide. The fallback contains no full topic tutorials, duplicate
Markdown copies, or scripting-section excerpt table. Release Markdown, catalogs,
archives, and website pages continue to use the complete canonical guides.

## Release and refresh

The deployment pipeline copies the Markdown tree to
`/ota/<version>/doc/manual/`, creates `doc/manual.zip`, generates
`doc/catalog.json`, and signs the exact catalog bytes as `doc/catalog.sig` with
the OTA release key. The schema-v2 catalog authenticates the archive path, byte
count, and SHA-256. Each page entry also contains its extracted path, byte
count, SHA-256, metadata, and Quick reference.

On a Wi-Fi, PSRAM, and SD capable build, `help update` resolves the configured
OTA URL to the exact running firmware version. The device verifies the catalog
signature, rejects version mismatches, downloads the one authenticated archive,
extracts it into a temporary revision, and checks every page's signed size and
SHA-256 before changing the small active-revision pointer. Existing readers
retain valid paths because activated revisions are not deleted at runtime.

An active signed catalog supplies the package-filtered topic directory and
Quick references; full page bodies are loaded from its verified files. If the
SD card, active pointer, signature, catalog, archive, page, or parser is
unavailable, each lookup falls back to its embedded setup reference or download
notice. A downloaded manual can be read offline while its storage is mounted.

Use:

```text
help status
help update
help reset
```

`help reset` removes the active pointer and immediately restores the embedded
setup and recovery guide. Cached immutable revision directories may remain on SD.

## PaperMono independent signed distribution

The custom PaperMono Board uses dedicated Docs settings. It does not inherit
the OTA key or OTA URL, and never writes OTA NVS. Other Boards with no Docs
overrides keep the legacy OTA-linked behavior above. The OS version and OTA
protocol are unchanged. Schema-v2 `firmware_version` on this custom build is
the Manual target **`4.16.1-papermono.1`**, shown by `help status`. Updates and
cached offline catalogs must match it exactly. Keep the identity for compatible
documentation corrections; bump its edition suffix together with the Firmware
when incompatible custom behavior needs a different Manual. Each content
release has a separate immutable revision derived from identity and content.

Build configuration (environment variables, before invoking the existing
Production build script):

- `SOLAR_OS_DOCS_RELEASE_URL`: a real HTTPS release base. An exact release base
  is used as-is; a terminal `/latest` is replaced with `/<Manual target>`.
- `SOLAR_OS_DOCS_PUBLIC_KEY_FILE`: path to the dedicated P-256 **public** PEM.
- `SOLAR_OS_DOCS_FIRMWARE_ID`: optional explicit Manual target override; normally
  leave unset to use the Board default above. It is not the OTA version.

No domain or production key is committed as a default. With either missing,
`help status` reports configuration required and `help update` fails before
network access or Storage writes. There is no fallback to upstream Docs. A
configured key can still verify a matching offline cache when no URL is set.
PlatformIO fingerprints these settings and the public key content so changes
reconfigure CMake; direct ESP-IDF users must explicitly reconfigure after
environment changes. The selected HTTPS base must directly serve artifacts:
the dedicated client disables redirects (including downgrade redirects), and
retains the HTTP service's normal certificate-bundle verification.

### Key custody

Use a separate production P-256 signing key stored outside the repository.
Generate it in a private local directory or secret-management environment;
the following is a procedure, not a key generated by this task:

```sh
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 \
  -aes-256-cbc -out /private/location/docs-signing.pem
openssl pkey -in /private/location/docs-signing.pem -pubout \
  -out /private/location/docs-public.pem
```

Restrict the private file permissions and back it up securely. Never put it in
Git, the web release directory or CI artifacts. Only its public counterpart is
a Firmware build input. For an encrypted key the packaging tool reads the
passphrase from `SOLAR_OS_DOCS_SIGNING_PASSPHRASE` if set (inject it through a
secret manager); it never prints it. Unset it after use. Without that variable,
the tool attempts an empty passphrase and fails rather than prompting forever.
The existing upstream OTA private key is neither assumed nor needed.

### Generate, sign and verify locally

Prerequisites: Python 3.11+ and OpenSSL 3.x CLI. Paths below are placeholders
for local operator-owned files, not configured public URLs. Use a new output
directory for each release; the tool refuses to overwrite an existing one.

```sh
python3 scripts/publish_manual.py build \
  --identity 4.16.1-papermono.1 \
  --private-key /private/location/docs-signing.pem \
  --public-key /private/location/docs-public.pem \
  --directory /local/release/4.16.1-papermono.1
python3 scripts/publish_manual.py verify \
  --identity 4.16.1-papermono.1 \
  --public-key /private/location/docs-public.pem \
  --directory /local/release/4.16.1-papermono.1
```

The tool reuses `generate_manual.py` and canonical `doc/manual/*.md`, preserving
topic IDs, aliases, section metadata and package gates. It creates exactly these
runtime artifacts after local verification:

```text
<release-base>/doc/catalog.json
<release-base>/doc/catalog.sig
<release-base>/doc/manual.zip
```

Catalog bytes are compact schema-v2 JSON. ECDSA P-256/SHA-256 signs those exact
bytes; `catalog.sig` is Base64 of the DER signature. Do not reserialize, newline
convert or edit the signed JSON afterward. Verification checks the signature,
target identity, bounded paths/sizes, archive hash and all page hashes. The
stage becomes the output directory only after those checks pass.

### HTTPS publication and later device use

1. Finish the local verification above, with the production key, correct target
   identity and current canonical content. Review the Manual's acceptance notes.
2. Copy the complete verified `doc/` directory to a new versioned directory on
   the operator's actual HTTPS server. Upload to an unpublished staging path
   and atomically expose it so Catalog/ZIP cannot mix across releases. Never
   publish a directory marked `TEST-ONLY.txt`.
3. Configure normal certificate trust and direct 200 responses for the three
   exact artifact URLs. Do not transform catalog bytes through a CDN. Never
   expose the private key or a passphrase. Test the served exact bytes locally
   against the selected public key; review access policy and availability.
4. Set Docs build variables to that real base and production public key, rebuild
   and follow a separately approved Application-only Flash procedure. Confirm
   local public-key SHA-256/build manifest and `help status` target/source/base.
5. **After SD writing is repaired**, mount the card, connect Wi-Fi and run
   `help update`. Existing Docs code verifies the signed Catalog, ZIP and each
   page before switching `active`; a rejected/interrupted update keeps the old
   revision or falls back to built-in Help when media is unavailable.
6. Verify a page, turn Wi-Fi OFF and reopen offline; safely unmount/remove the
   card and verify embedded fallback. Never delete revisions being read.

Actual server publication, Flash and SD operations were not performed during
this integration. PaperMono **new SD saves remain FAIL**. Wi-Fi download,
device SD install and offline-read acceptance are **BLOCKED** until SD writing
and production URL/key provisioning are complete. Do not bypass signature
verification, move Docs to Flash or format automatically to evade that block.

### Host verification scope

`make -C tests/host test-signed-manual CJSON_DIR=<installed-IDF>/components/json/cJSON`
runs generation/signing tests and compiles the actual Docs, JSON and ZIP
update/install implementation against temporary virtual Storage. Linux GCC,
OpenSSL development headers/library, zlib development headers/library and the
existing installed ESP-IDF cJSON/miniz headers are required. Tests use temporary
P-256 keys, fake `.invalid` HTTPS endpoints with no network access, OpenSSL
cryptographic primitives, and a zlib raw-deflate adapter for the ROM API.
Package filtering, revision activation, old revision retention, offline reads,
tampered signature/key/catalog/archive/page, path rejection, limits, download
interruption, virtual media disconnect and URL/OTA isolation are exercised.
These are not tests of real ESP TLS, mbedTLS, ROM inflate, Wi-Fi or SD hardware.
