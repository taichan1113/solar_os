# PaperMono 独自Manualの署名付き配布統合

作業日：2026-10-27 JST。判定：**Host上の配布基盤は完成。本番提供・実機AcceptanceはBLOCKED**。

## A. 実装結果

独立Worktree：`E:\ESP32\M5PapserMono-SolarOS\.research\signed-manual-distribution\worktree`。
Branch：`feature/papermono-signed-manual-distribution`。
基点：`729da1caa3c518054f27064a6ee04a20fb999030`（Sketch比較・Shell整理を含む）。

| 項目 | 実装 |
|---|---|
| Docs専用設定 | `SOLAR_OS_DOCS_RELEASE_URL`、`SOLAR_OS_DOCS_PUBLIC_KEY_FILE`、必要時`SOLAR_OS_DOCS_FIRMWARE_ID`のBuild環境変数 |
| PaperMono既定 | 専用Docs、URL空・公開鍵空。更新前に設定待ちエラー。OTAへfallbackしない |
| 他Board既定 | Docs overrideがなければ従来のOTA URL・OTA鍵・OS version照合を維持 |
| 独自版識別 | Manual target **`4.16.1-papermono.1`**。OS/Firmware/OTA version **4.16.1は変更しない** |
| Catalog | schema-v2を維持。`firmware_version`にManual targetを入れ、更新時と専用版のoffline cache読込時に完全一致を要求 |
| revision | 配布補助ツールがidentity＋内容から16桁hexを生成。同じ本文でも別Manual targetへ同じrevisionを流用しない |
| URL解決 | `<base>/latest`→`<base>/<Manual target>`、既に特定releaseを示すURLはそのまま。末尾slashを除去して`/doc/…`を結合 |
| Help | `help status`にManual target、Docs source、設定済みなら解決後URLを表示。独自版を常にoutdatedと誤表示しないよう既存警告もtargetと比較 |
| 生成・配布補助 | 既存`generate_manual.py`を利用する`publish_manual.py build/verify`。ローカルでのみ生成し、検証済みstageを出力directoryへ移す |
| 配布構造 | `<release-base>/doc/catalog.json`、`catalog.sig`、`manual.zip` |

`generate_docs_config.py`が専用Build headerを生成する。PlatformIOの既存selection stampにDocs設定・公開鍵内容のfingerprintを追加し、設定変更後も古いheaderを再利用しない。OTAのNVS設定、URL取得関数、署名鍵、更新処理は変更していない。

Canonical sourceは`doc/manual/*.md`のまま。既存topic ID/aliases/section/package gatesを維持し、`boards.md`へPaperMono専用節、`help.md`へ独自配布・制約の説明を追加した。F全App、fastest/tile/20MHz/rows-dual/32回同期/Async/pacing OFF、A/F復旧、3列×2行Launcher、Manual touch、USB Shell、Wi-Fi保存OFF尊重、手動NTP、Writer保存/Checkpoint、Sketch local、TypeMouse固定/反転、Flash `/`・SD `/sdcard`、安全なUnmountを記載した。

**SD新規保存FAIL、実機Manual更新未完成を明記**。修正済みSketch lease/CANCELやSD再挿入の実機PASSを、SD書込み・Manual更新PASSへ広げていない。

主な根拠：

- `src/services/solar_os_docs.c`：`docs_verify_signature`、`docs_parse_catalog`、`solar_os_docs_release_base`、既存`solar_os_docs_update`/revision activation。
- `src/CMakeLists.txt`、`scripts/generate_docs_config.py`、`scripts/platformio_solaros_flavor.py`：Docs専用設定・再構成。
- `src/shell/solar_os_shell_manual.c`、`src/apps/solar_os_docs_app.c`：status/版警告。Touch操作自体は変更なし。
- `scripts/publish_manual.py`：生成、署名、署名済みbytes・ZIP/全pageの検証。
- 詳細運用手順：[manual-system.md](manual-system.md#papermono-independent-signed-distribution)。

## B. Security

- **ECDSA P-256 / SHA-256**。署名するのはCatalogの正確なbyte列。OpenSSLのDER署名をBase64化して`catalog.sig`へ保存。署名後JSONの再整形は禁止。
- Build用鍵はP-256 public SubjectPublicKeyInfo PEMのみ。private PEM、別形式/curveを拒否。実際の署名検証も既存crypto APIを維持する。
- 本番秘密鍵は作成・Commitしていない。Hostテストは一時directoryで別の鍵を生成し、終了時にprivate fileを削除。ローカル配布見本も**TEST-ONLY**で、本番Firmwareの信頼鍵として使っていない。
- 本番署名鍵はRepository/公開directory/CI artifactの外で管理する設計。暗号化鍵のpassphraseは必要時だけ`SOLAR_OS_DOCS_SIGNING_PASSPHRASE`から取得し、値をログへ出さない。
- 専用sourceはHTTPSのみ。credentials/query/fragmentや不正長のURLをBuild時に拒否する。専用HTTP requestはredirectを許可せず、HTTP downgradeも起こさない。既存HTTP serviceの証明書bundle検証を変更しない。
- ZIPのsize/SHA-256、全pageのsize/SHA-256、相対path・metadata検証、stage作成、完了後active切替、旧revision保持を既存Cコードで維持する。
- OTA変更なし。`keys/ota_signing_public.pem`のSHA-256は基点と同じ：`ec172ecf21734c6f4da64d2b1920d6fd451f89e419cfc15803ca4c43eb2e5366`。

他Boardでは従来の署名済み旧version cache＋警告を維持。専用PaperMonoは別targetのcacheも拒否し、上流4.16.1を独自版として表示しない。

## C. Host／Build

### テスト

**PASS：Production Host 32 binaries＋Async追加3条件。Python 66件（既存52＋追加14）。**

| 検証 | 結果・範囲 |
|---|---|
| 正常系 | canonical全Manual生成→テスト鍵署名→対応鍵検証→実C Docs/JSON/ZIP install→package選別→active切替→旧revision残存→offline再読込 PASS |
| 改ざん・署名 | JSON意味が同じでもbyte変更を拒否、不正DER、別鍵署名、ZIP改ざん、展開後page改ざんを拒否 PASS |
| 版 | `4.16.1`の署名済みCatalogを専用targetが更新時/起動cache時とも拒否 PASS。legacy旧版保持は既存Docs media testでPASS |
| path・上限 | Catalogの不正相対path、署名済みZIP内`../`、空/破損/上限超過Catalog、archive/page size上限を拒否 PASS |
| 中断・媒体 | download中断、展開中の仮想SD書込み失敗/切断でactive pointerを変更しない。切断中fallback、再挿入相当のreloadで旧Manual保持 PASS |
| 設定 | 未設定時network request 0、専用設定でOTA URL read 0、`/latest`/exact URL解決、legacy BoardのOTA連動、private/不正public key拒否、fingerprint変化 PASS |
| 既存回帰 | Manual metadata/生成、Board、Wi-Fi TUI、F/Driver/Async、Sketch lease/PNG、Writer、TypeMouse、Launcher等の既存Production tests PASS |

新E2Eは`tests/test_signed_manual.py`＋`tests/host/docs_distribution_test.c`。**実CのDocs・JSON・ZIPサービス**を使用するが、暗号primitiveはHost OpenSSL、ROM deflate primitiveはzlib adapter、HTTPSはlocal fileから分割応答するstub、Storageは`/tmp`の仮想directory。実ESP TLS・mbedTLS・ROM・Wi-Fi・SDのAcceptanceではない。既存Docs media testのmock cryptoと、新E2Eの実OpenSSL検証も区別した。

初回E2Eの失敗はHost仮想rootが長く、Firmwareの有限path長を消費したfixture条件に起因。deviceに近い短い仮想mountに修正した。媒体切断中に読めるとする誤ったfixture期待値も修正し、fallback→再挿入後旧版readを検査した。既存Pythonのsource guardは専用identity照合に更新。これらを実機不具合や実機修正として扱わない。

再現：

```sh
make -C tests/host -j4 \
  FATFS_DIR=/path/to/framework-espidf/components/fatfs \
  CJSON_DIR=/path/to/framework-espidf/components/json/cJSON test-production
make -C tests/host CJSON_DIR=/path/to/framework-espidf/components/json/cJSON test-signed-manual
python3 -m unittest tests.test_signed_manual tests.test_generate_manual \
  tests.test_board_manifests tests.test_wifi_tui_scan_flow
```

### ローカル署名済み配布見本（TEST-ONLY）

Directory：`E:\ESP32\M5PapserMono-SolarOS\.research\signed-manual-distribution\artifacts\test-release`。

Identity `4.16.1-papermono.1`、revision **`b8aeb8830ec23c8c`**、**253 pages**。

| File | Size | SHA-256 |
|---|---:|---|
| `doc/catalog.json` | 339,825 B | `bd1d6912ebd4a7650e97a7cca7f7b76ba47614a2d44c1f0f3be344e2f2a5f62d` |
| `doc/catalog.sig` | 97 B | `ab7f12d1b291ffbe6037cd035627bdbf808b9f8e91f57687c26926ba06550521` |
| `doc/manual.zip` | 457,954 B | `fe25d8278dda358afbd4b92b5a2bda9f3292ddc70faa2ab2d4ac8ed81b78497c` |

`HOST-TEST-PUBLIC.pem`、`TEST-ONLY.txt`、`artifact-audit.json`を同梱。public PEM SHA-256は`030934ea94c6052ce8d69e39fda53d1792e801a19f7eeeb80552d1317868fd85`。private鍵は保持していない。**この見本を本番公開しない。本番鍵・本番URLでの成果物ではない。**

Catalog/ZIPは既存上限512 KiB/2 MiB以内。253 pagesは現行上限256内だが、今後のtopic追加時は上限確認が必要。配布見本はローカルignored領域、生成ツール・テスト・Canonical本文・報告書はGitで共有する。

### Firmware Build

**SUCCESS**（243.24秒）。既存`scripts/build_papermono_production.ps1`を使用。現行PlatformIO `esp32_s3_devkitc1_n16r8`、pioarduino 55.3.312、ESP-IDF **5.5.5**、Xtensa GCC **14.2.0+20260121**。SDK更新なし。

Board `m5paper_mono` / Flavor `writerdeck` / Layout `ota`。既存flagsはWRITER_D=1、SPI20_TRIAL=1、DIRTY_HOLD_TRIAL=1、HYBRID_TRIAL=0、WAVEFORM_TRIAL=0、SKETCH_LATENCY_TRIAL=0。LUT/電圧/Driver、tile/rows-dual/32回同期/Asyncやアプリ描画は変更なし。

BuildしたコードCommit：**`fe7ab76c119e4fb5644b5e444cc299deca103be9`**。その後の差分は手順・報告・台帳のみ。

| 成果物 | Size | SHA-256 |
|---|---:|---|
| `firmware.bin` | 3,514,320 B | `371aeccbf564828874c54e15a5745e2594786e2ebe2b3cc584e7a0a5316e73cb` |
| `firmware.elf` | 30,952,072 B | `87f11d3aef08cef93ba190907d58c051e12f84649f50060c043e7daf82e7d506` |
| `partitions.bin` | 3,072 B | `fd976d7c1eaa8f9d0a01ac87a8d05fc1cb1e59d3c5970954b30ad0c3aa12a488` |

Binary絶対パス：

`E:\ESP32\M5PapserMono-SolarOS\.research\signed-manual-distribution\artifacts\fe7ab76c\firmware.bin`

同archiveにELF、partition比較用binary、生成Docs設定header、Build/Host/Python logsを保存。元出力はWorktreeの`.pio/build/esp32_s3_devkitc1_n16r8/firmware.bin`。リンク時RAM 55,400 / 327,680 B、Flash 3,508,295 / 7,340,032 B。

使用したDocs設定は**URL空、public key空、target `4.16.1-papermono.1`、専用mode=1**。本番署名鍵・Hostテスト鍵のどちらも信頼鍵へ入れていない。生成headerとbinary内のcustom identity/configuration-required文字列を確認。binaryに`https://docs.invalid`がないことも確認した。

Partition binaryは基点の既知hashと一致。OTA公開鍵file、OTA更新コード、version.txt、Driver、既存Production Build scriptにもGit差分なし。自動生成されたfactory結合imageは使用していない。実機は接続していない。

## D. 未完了項目と状態

1. **本番HTTPS公開先**：未指定。実在domain/配布base、公開権限・certificate/CDN方針が必要。架空URLはFirmwareへ組み込んでいない。
2. **本番Docs署名鍵**：未指定。ユーザー管理のP-256秘密鍵と対応public PEMが必要。OTA鍵とは分離する。
3. **公開作業**：未実施。検証済み`doc/`をversioned HTTPS releaseへ一括公開する手順は完成。
4. **SD新規保存FAIL**：未修正。SD driver・FatFS・保存先・formatを触っていない。
5. **実機Manual更新/SD install/offline read**：SD保存修正と本番provisioning後まで**BLOCKED**。

COMアクセス、Flash、startup/NVS変更、実SD書込み/format、OTA実行、実サーバー配信、Merge/force-pushは行っていない。実機は依頼書記載の実用Firmwareから変更していない。startup4行削除・正常rebootはユーザー報告の完了状態として扱い、それ以外のstartup内容を変更済みとは仮定しない。

## E. 次の実機Acceptance（SD保存修正後、別途承認）

1. 本番keyをprivate環境で準備し、対応public PEMと実在HTTPS baseをBuildへ指定。Manual target、public key hash、Catalog/ZIP/page検証、実際に配信される正確なbytesをローカルで再確認する。
2. Application-only手順で実用App backup、chip/partition/boot slot、NVS/FAT/otadata等保護を照合してFlash。Bootloader/partitionは変更しない。Rollbackを用意する。
3. Launcher/USB Shellで`help status`を確認。Firmware 4.16.1、Manual target、専用source、解決後URLが想定どおりであること。OTA URL/NVS/OTA鍵が従来どおりであることも確認する。
4. 修正済みSDのmount/専用ファイル書込み確認後、Wi-Fi接続→`help update`。署名付きCatalogの検証、ZIP/page verification、active revision切替をログで確認。旧Manualを残す。
5. Manual touch/ReaderからPaperMono操作節を開く。Wi-Fi OFF後に再読込してoffline閲覧を確認。文書を閉じて安全にUnmount/抜去し、内蔵Helpへfallbackすることを確認する。
6. 管理された試験配布元で版違い・署名改ざん・ZIP/page破損を拒否し、旧Manualまたは内蔵Helpを維持することを確認。署名無効化、HTTP化、format、Flashへの無断移設はしない。

## Commit一覧

| Commit | 内容 |
|---|---|
| `052fd11d0b5607bc28fd81084a586ec9c31ab8ba` | Docs専用source/trust/identity、Help表示、Build設定 |
| `177c66fa2ec87010f4a4d1fb8f79524a14140166` | Canonical ManualのPaperMono内容・Acceptance制約 |
| `02adf6e4fdfc70e4e9b6c1c94fca7985da7a6f6a` | ローカル生成/署名/検証/配布stageツール |
| `fe7ab76c119e4fb5644b5e444cc299deca103be9` | 実C installerを使うHost E2E・回帰テスト |

手順・報告の文書Commitはこの後のBranch HEADを参照。作業Branchだけをoriginへ通常Pushする。
