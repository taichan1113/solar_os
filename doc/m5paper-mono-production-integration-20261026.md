# M5Paper MONO 本採用Firmware 統合実装報告（2026-10-26）

**Firmware実装完了＋実機未確認。** 関連Host回帰とESP32-S3ビルドは成功した。今回はUSB/COMアクセス、Flash、実機入力、実機startup/NVS/SD上の変更を行っていない。既存Integration/mainへのMergeも行っていない。

## A. Git・成果物

- 基点：`aa08bbfdfa65c72ada0a0bc7737d36b47a521186`（`integration/papermono-dirty-sketch`）。
- 作業Branch：`feature/papermono-production-integration`。
- Worktree：`E:\ESP32\M5PapserMono-SolarOS\.research\production-integration\worktree`。
- Push先：`origin/feature/papermono-production-integration`（`taichan1113/solar_os`、通常Pushのみ）。
- FirmwareコードCommit：`ea7b10e49a0a191947b3f227e855e9335a7e0616`。後続Commitは報告書・台帳のみ。
- 既存39 WorktreeのHEAD・tracked差分・既存未追跡ファイルを照合し、不変。元Integrationは`aa08bbfd`、未追跡5ファイルも維持。証跡はローカル保管物の`preservation-checks.json`。

| Commit | 内容 |
|---|---|
| `48051438` | Flash root／startup実効元を先行保護 |
| `67487f7d` | F配列を変更せず全App既定化、A/F復旧・Trace |
| `88a489fe` | USB/Wi-Fi Boot、Shell Esc、Launcher、Manual Touch |
| `03d2a66e` | SDMMC 4bit、IOE電源・検出、旧1bit互換 |
| `755d4e59` | Fの実Driver＋Async回帰 |
| `1124551d` | SD状態変更時の署名付きManual再評価・排他 |
| `aed64a1c` | Build専用スクリプト、画面/USBの復旧案内 |
| `ea7b10e4` | Manual索引を一度だけ公開し直前世代を保持 |

Fの参照元は`70e1c0ffeaf4d4e9e616d47a28ee3e5aaf0386cc`。研究Branch全体は取り込まず、必要な配列・選択・診断を既存Driverへ追加した。[過去のF光学評価](m5paper-mono-candidate-f-optical-evaluation-20261022.md)は今回の実機PASSを意味しない。

## B. 実装結果

| 項目 | 実装・検証 | 残る確認 |
|---|---|---|
| F `00 C0` 全アプリ既定 | Driver/Async Host PASS | SketchもF。光学品質・長期DCバランス・寿命は今回未評価 |
| fastest/tile/20MHz/rows-dual/32周期/Async | 通常PaperMono構成に適用 | 実効クロック・実機での経路確認 |
| USB Shell自動起動 | Bootから既存API、startup非実行 | 実USB再接続・重複防止の実機確認 |
| Wi-Fi Boot ON | 保存OFF尊重、保存済みSTAへ接続要求 | 実ネットワーク確認 |
| NTP非自動起動 | Bootから呼出しを追加しない | 未読の実機startupにある自動行は別途整理 |
| Shell空行Esc | Launcherへの特殊分岐のみ削除 | 実キー確認。取消・明示launcher・他App/KEY2維持 |
| Launcher | 2行×3列、指定順。Host PASS | 実表示・タッチ確認 |
| Manual Touch | 既存Help/Readerを拡張。Host PASS | 実座標・光学表示確認 |
| microSD | 既存SDMMC/Board storage拡張。Host PASS | 電源・検出極性・実I/O・Sleep/Wake |
| Flash root/startup | SD有無に依存せずFlash優先。Host PASS | 保存NVSは保持、実機ファイル確認 |
| SD Manual | 署名/hash/版警告を維持し再評価。Host PASS | 正式署名済み実カードで確認 |
| TypeMouse／Sketch局所描画 | アプリ本体を変更せず回帰PASS | 新Frame、新Sketch selectorは今回追加しない |

## C. 主要変更

### EPD F既定化

[SSD1677 service](../src/services/solar_os_ssd1677.c#L535)がPaperMonoだけ`default_f=true`を渡す。[Driver初期化](../src/drivers/epd_ssd1677.c#L881)でF/fastest/20MHzを選び、既存tile確保・rows-dual・Asyncを再利用する。Fは[reference.inc:41](../src/drivers/epd_ssd1677_reference.inc#L41)の110バイト。参照実装と同一で、Aとの差はLUT0 `00 00`→`00 C0`だけ。電圧値・他Group・駆動手順は変更しない。

初回OTP基準生成、cleanup、Sleep/Wake後のOTP再生成は保持。その後の通常fastestはFを選び、更新後にBaseline LUTをレジスタへ戻す。次回もFを再ロードする。[選択・失敗停止](../src/drivers/epd_ssd1677_writer_d.inc#L26)。既存32回カウンターの全RAM同期を維持し、追加`0x20`を発行しない。無変更画像の更新skipも維持する。

160ms burst、新pacing、新波形・新電圧は追加しない。旧define名`WRITER_D`／`SPI20_TRIAL`／`DIRTY_HOLD_TRIAL`は依存関係維持のため残すが、通常構成ではFとrows-dualが有効。productionではD選択を拒否し、waveform/hybrid研究flagは0。研究Branchは維持する。

画面Shell／USB Shellの復旧・診断：

```text
display test display0 fastest-lut status
display test display0 fastest-lut baseline
display test display0 fastest-lut candidate_f
display test display0 fastest-lut trace
display async display0
display mode display0 refresh=cleanup
```

`disable`はBaseline A選択の互換名、`writer-lut`は旧alias。選択自体は表示更新せず、NVSへ保存しない。失敗ラッチ後はコマンドで無理に再試行せず、状態確認・再起動・正当なRollbackを行う。

### Boot／startup／Shell／Launcher

[Board Boot](../src/services/solar_os_board_boot.c#L11)からCDC登録後にUSB Shellを生成する。`run_startup=false`で既存worker/Sessionを再利用。同じPaperMono `cdc0`に停止要求のないSessionがあれば、そのIDを返して再生成しない。[重複防止](../src/services/solar_os_port_shell.c#L1662)。画面内Shellによるstartup一回実行は維持する。

Wi-Fiは保存OFFなら起動しない。未設定/ONでは既存start/connect_savedを呼び、保存profileで`esp_wifi_connect`を要求する。接続完了待ち・scan待ちは追加せず、接続先なしもBoot失敗にしない。AP/NAT/repeater/NTPの自動開始を追加しない。

Shellの空行Esc→Launcher分岐だけを削除した。非空行Escの取消、明示`launcher`、USB Shell、他AppのEsc、KEY2長押し判定は維持する。

[Launcher配列](../src/apps/solar_os_launcher.c#L485)：

| | 左 | 中央 | 右 |
|---|---|---|---|
| 上段 | Manual (`help`) | Reader (`files --reader`) | Wi-Fi |
| 下段 | Keyboard (`typemouse`) | Sketch | Clock |

Sleepヘッダー・CHILD_RETURN・タッチhandoffを維持。Files/Shellは枠から外すだけで機能を削除しない。他BoardのJSON経路と実機の既存JSONは変更しない。

Wi-FiはBoot OFFでも既存TUIをCHILD_RETURNで開く。開くだけではON/NVS変更しない。radioの明示操作で次回起動設定を変え、再起動が必要と表示する。Wi-Fi TUI全体の新規タッチ操作は追加していない。

### Manual Touch

[既存Docs App](../src/apps/solar_os_docs_app.c#L477)でterminal hit-cellとapp_touch leaseを再利用。releaseでsectionを開閉し、pageなら既存Readerを子起動する。スワイプはviewportを動かしtapと区別する。MOVEから直接EPD更新せず、変更時だけrender_pendingを立て既存TICK/Asyncで集約する。

Reader復帰でcursor/top/折畳みstateを保持。通常suspendではlease解放、resumeで再取得。CANCELのbalanced suspend/resumeでは余分な電源再投入・再描画を行わない。従来キー操作・USB Helpを維持し、新Taskは追加しない。

### SDMMC・保存先・Manual連携

[manifest](../boards/manifests/m5paper_mono.toml#L102)にstorage0を追加。CLK13/CMD12/D0=11/D1=10/D2=9/D3=8。電源`ioe0:13`（HIGH=ON）、検出`ioe0:0`（LOW=挿入）。ESP GPIO13とIOE line13は別resource。既存M5IOE providerのread-modify-writeで他lineを保持する。

[SDMMC service](../src/services/solar_os_sdmmc.c#L84)へGPIO_LINE電源/検出bindingとExpansion lifecycle callbackを追加。旧native-GPIO電源bindingと1bitは残す。初回ONだけ300ms待機し、描画入力経路へ待機を増やさない。電源・検出I2Cエラーを`no card`へ丸めない。SD未挿入・初期化失敗でもFlashが使えればBoot継続。早期attach失敗も既存main/Expansionの警告・継続経路を通る。

Light sleepではカード電源・SDMMC Host・mountを保持する。suspended中の新規カード初期化は拒否、Wakeで解除。mounted媒体の電源断は追加しない。実際の進行中I/O・電源保持・消費電力は実機未確認。background hot-plug Taskは作らず、再挿入は既存diskのunmount/rescan/mountを使う。抜去時の既存I/Oエラーは成功扱いにせず、無制限retry・自動formatを追加しない。

[PaperMono storage policy](../src/services/solar_os_storage.c#L94)はFlash `/`、SD `/sdcard`。文書、Sketch、履歴、設定を移動しない。Flash mount失敗をSD成功で隠さない。保存済みNVS startup_src=AUTO/SD/FLASHを消さず、PaperMonoの実効startupだけFlashへ固定する。`setterm startup`で保存値とEffective startupを照会できる。

[Docs媒体変更guard](../src/services/solar_os_docs.c#L1131)は更新job/page read中のmount/unmount等を拒否し、操作後に既存`solar_os_docs_init`で署名catalogを再評価する。索引公開は一回にし、直前世代を保持する。`help update`のDL・署名・hash・activateを維持。オフラインSD閲覧、SDなし/不整合時の内蔵fallback、古い版の警告は既存機構を利用。HTTP配布元、OTA鍵、署名方式は変更しない。

## D. テスト・Build

| 検証 | 結果・限界 |
|---|---|
| `make -C tests/host test-production` | **PASS：27実行ファイル＋Async追加3経路** |
| F Driver | 全入力元、skip、A/F、Sleep/Wake、32周期同期、Baseline復元 PASS |
| Driver＋Async | Other/Writer/SketchでF、RAM/Shadow、復元失敗時のpending停止・再試行なし PASS |
| Sketch/TypeMouse/Writer | Canvas/PNG保存再読込、Session adapter、TypeMouse engine/lifecycle、Writer checkpoint PASS |
| Launcher/Manual | 6項目、JSON非破壊、tap/fold/swipe、Reader復帰、lease/CANCEL/Sleepモデル、従来キー PASS |
| Boot | 保存OFF、接続先なし/失敗、USBにstartupを渡さない条件 PASS。重複guardはソース確認、実USB再接続は未確認 |
| storage/SDMMC | Flash優先、startup全組合せ、SDエラー時Boot、IOE/4bit/旧1bit、mount通知・競合拒否 PASS |
| Docs | 実サービスのSD absent/present/reinsert、旧版metadata、offline page、検証拒否・旧索引保持 PASS |
| Python | Board manifest **33件**、Manual生成 **13件**、Wi-Fi TUI **6件** PASS |
| Board metadata validator | **全Board PASS**。他BoardのFirmware Build/実機試験は未実施 |
| Firmware Build | **SUCCESS**：PlatformIO 55.3.312／ESP-IDF **5.5.5**／Xtensa 14.2.0+20260121。SDK upgradeなし |

Docs媒体Host試験の暗号primitiveはstubであり、暗号方式自体の新しい実証ではない。本番の署名/hash検証を無効化する変更はない。Host PASSから実カード・電気・光学品質を保証しない。

WindowsのBoard試験は`_curses`不足で開始できなかったため、WSLで同じ33件を実行してPASS。途中のfixture不足・期待値を修正し、最終スイートは全PASS。未解決のBuild失敗はない。

再現手順（Flashを含まない）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_papermono_production.ps1
```

```bash
# WSL / repo root。CJSON_DIRは使用SDKのcJSONを指定
CJSON_DIR=/mnt/c/Users/takec/.platformio/packages/framework-espidf/components/json/cJSON make -C tests/host test-production
python3 -B scripts/validate_board_metadata.py
python3 -B -m unittest discover -s tests -p '*board*.py'
python3 -B -m unittest discover -s tests -p test_generate_manual.py
python3 -B -m unittest discover -s tests -p test_wifi_tui_scan_flow.py
```

## E. 実機Acceptanceへの引継ぎ

### Firmwareと保護対象

- 保管物：`E:\ESP32\M5PapserMono-SolarOS\.research\production-integration\artifacts\production-20261026\firmware.bin`
- Build出力：`.pio/build/esp32_s3_devkitc1_n16r8/firmware.bin`
- サイズ：**3,516,816 B**（7MiB app slot以内）。
- SHA-256：`86b735ef56e2a5a6f4400bb5d02985ee5c127b10f85eb165e3d1337d1051973b`
- 設定・partition・成果物情報：[JSON](m5paper-mono-production-integration-artifact-20261026.json)。
- 自動生成された`firmware.factory.bin`はApplication-only投入対象ではない。
- partition CSVは基点から変更なし。ただし本体のpartition/起動slot/Flash容量は今回読んでいない。過去のCOM39/offsetを無確認で使わない。

現行**本体**のFirmware/startup/NVSは未読で不変。基点コードとの差分を、本体との差分と同一視しない。Acceptanceでは機体・slotを読取りで確定し、現在appと保護領域をbackup/hash照合してからApplication-only投入する。bootloader、partition table、otadata、NVS/PHY/FAT、BLE bond、文書・履歴・設定を保護する。

### 初回起動・人手確認

1. 画面Shell/startupの後にLauncher。startup overrideがなければF/fastest/tile/20MHz/rows-dual/Async。初回OTP更新は既存の正常処理。USB Shellはstartupなしでも生成される。
2. status/Trace/Asyncで選択・クロック・Snapshot集約・32周期同期を確認。光学品質は目視で別評価する。
3. Launcher 6項目→Manualのtap/fold/scroll→Reader→戻る、Sleep/Wakeと最初のタッチ、他App復帰。空Shell Escはその場、入力中Escは取消、明示launcherは起動。
4. USB/画面Shell同時使用、旧USB作成startup行が残る場合の重複防止、抜き差し、startup一回。
5. Wi-Fi未設定/保存ON/保存OFF、AP不在時Boot、OFF時TUI、NTP非自動起動。資格情報を報告書へコピーしない。
6. SDなしBoot・既存ファイル保持、検出・mount/unmount/rescan/reinsert、読書き、使用中抜去エラー、Light sleep/Wake後のmount/読書き。**SD formatは行わない。**
7. 正規署名Manualのオフライン閲覧、版警告、内蔵fallback、help update、update中unmount拒否。署名無効化は行わない。
8. Writer保存再読込、Sketch黒線/末端/PNG、TypeMouse固定/反転/BLE HID、Launcher復帰。

### startup整理案（未適用）

実機原文は未読。以下は該当行がある場合の**行単位候補diff**で、全体置換ではない。Acceptanceで原文backup/hash→差分確認→必要行だけ整理し、BLE/font/zoom/ユーザースクリプト等は残す。

```diff
- session create shell cdc0 --term dumb --size 80x25
- display mode display0 refresh=fastest
- display mode display0 fastest-path=tile
- display mode display0 spi-mhz=20
- display mode display0 dirty-window=rows-dual
- lut 5
```

既定と重複する場合だけ削除候補。`lut 5`は旧研究版のF選択例で、本版startupへ追加しない。別波形・hybrid/typing/dose/hold・timing・自動試験行は既定を上書きするため個別整理。NTP自動行（`job start ntp-sync ...`等）も除去候補。Wi-Fi/保存接続の重複行はBoot確認後に整理する。**今回は実機のどの行も削除していない。**

startup行の整理と機能・コマンド削除は別。手動Wi-Fi/NTP、Files/Shell、A/F、cleanup、Trace/Async、disk/Helpは維持。研究Branchを削除しない。保存NVS startup sourceも消さず、実効Flashと区別する。

### Rollback

今回はFlashしておらず、本体を戻す操作は不要。Acceptance前に、その時点の実用app原本と保護領域hashを保存する。Rollbackはbackup元の検証済みapp partitionへ当該appだけを書き戻し、読戻しSHAと保護領域を照合する。Integration `aa08bbfd`も保持したが、その再Buildを「現在本体の正確なbackup」の代用にしない。

## F. 次回へ残す項目

1. Acceptanceで判明したハードウェア依存不具合。特にmicroSD、USB再接続、各AppでのF表示品質。
2. Sketchの現行局所描画と追加高速化候補の同条件実機比較・必要なShell selector。今回はCanvas/更新方式を変更しない。
3. 独自Firmware用Manualの正規署名付き配布。HTTPS/鍵/OTA配布先は今回変更しない。

Fの長期DCバランス・寿命は未評価として記録する。今回の統合完了を、長期評価や実機AcceptanceのPASSとは扱わない。
