# Writer Candidate D 常時適用 — Integration反映報告

2026-10-18。依頼「Candidate D常時適用・Integration優先実装」に対応。

## 最終状態

| 項目 | 結果 |
|---|---|
| 実用Integrationへの反映 | 完了、fast-forward。`integration/papermono-dirty-sketch` |
| 実装Commit | `fb417d06ce4718913ea0ae402600987cacf159dd` |
| 機能Branch | `feature/papermono-writer-candidate-d` |
| 基点 | `391925abff2d261d1d5856711aa0a1f101ac435a` |
| ESP32-S3ビルド | PASS、warning/error 0/0 |
| Hostでの連続D | 1・6・7・20・21回目D、非同期経路23回までD：PASS |
| 今回の実機Flash | 未実施。COM列挙・実機アクセスも未実施 |
| 実機Writerでの連続D・光学品質 | 新Firmwareでは未確認。接続・Flash承認後に通常手入力で確認 |

機能Worktree：`E:\ESP32\M5PapserMono-SolarOS\.research\writer-d-integration\worktree`

実用Integration Worktree：`E:\ESP32\M5PapserMono-SolarOS\.research\dirty-sketch-integration\worktree`

本資料・証跡だけの後続CommitもIntegrationへfast-forwardする。上記実装Commit以降、ビルド対象ソースの追加変更はない。pushは行わない。

## 実装

既存Writer Trialの発生元タグ、非同期Executor、LUT選択・復帰、USB Shellを再利用した。既存ソース3 Commit（元 `029724b0` / `695809f6` / `25123282`）を基点へ適用し、それぞれ `9be3ef8d` / `4731fc15` / `5f4c92b5` として保持。その上の `fb417d06` で標準化・上限撤廃を実施。過去のFlash・診断専用変更は取り込まない。

- PaperMonoでは `SOLAR_OS_SSD1677_WRITER_D=1` を標準とし、Writerのfastest物理更新は起動時からD。累積6回上限、到達時の自動Baseline移行、7回目以降の拒否を削除した。周期的なD間引きもない。
- Writer描画の発生元をGFX→Display→既存の不変スナップショットへ渡す。更新実行時の前面アプリ推定は用いない。Shell・Launcher・Sketch・TypeMouseなどの更新はA。
- DのLUT0は既存の `2F F0`。Baselineとの違いは110バイト中0・1のみで、2〜109、LUT1〜4、Timing/FR、Gate/Source/VCOM値は不変。
- 初回の実際のcustom fastestからDを使う。既存の起動・初期化に必要なOTP更新は変更せず、OTPをD更新と数えない。
- LUT選択からRAM転送、既存`0x22/0x20`、BUSY、両面RAM同期、Baseline LUT復帰まで、既存Executorの単一更新区間内で処理する。復帰に追加`0x20`はない。温間`0x22=0x0C`、初回custom `0xCC`は既存どおり。
- fastest＋tile、20MHz、Dirty rows-dual、既存RAM転送範囲、Shadow、no-op省略、要求集約を維持。1文字につき強制物理更新する実装ではない。既存Dirtyの32更新ごとのRAM全同期は整合性維持であり、Dを解除・間引きする上限ではない。
- 累積カウンタは飽和するuint64診断値。桁あふれでもD停止に使わない。直近32件のtraceは独立した有界添字で管理し、累計飽和後も動く。入力メタデータの32bit値も飽和加算する。
- BUSY timeout・LUT送信・同期・Baseline復帰失敗ではD停止とDriver faultを維持し、状態不明のままAで更新継続したり自動再試行したりしない。既知の非対応設定ではAへ戻る。

主要実装箇所（上記実装Commit）：

| 箇所 | 内容 |
|---|---|
| `src/drivers/epd_ssd1677_writer_d.inc:5` | 診断カウンタ飽和加算 |
| 同`:23` | 発生元・モード・正常状態によるD選択。回数上限なし |
| 同`:49`以降 | 結果記録・異常停止・trace・モードAPI |
| `src/drivers/epd_ssd1677_reference.inc` | 既存D LUT、更新区間内のLUT選択と復帰 |
| `src/services/solar_os_display_async.c` | 既存Executor・更新境界と発生元管理 |
| `src/services/solar_os_display_submission.h` | スナップショットへ発生元を付加 |
| `src/apps/solar_os_writer.c` / `src/services/solar_os_gfx.c` | Writer由来描画タグ |
| `src/CMakeLists.txt:529` / `scripts/platformio_solaros_flavor.py:99` | PaperMono標準D・Dirty・SPI20。明示0は尊重 |
| `src/shell/solar_os_shell_writer_lut.inc:25` | 上限表記のないstatus、64bit値 |
| `README.md:222` / `src/shell/solar_os_shell_commands.c:282` | 標準D・Baseline切替の説明 |

全変更ファイルは証跡の `changed-files.txt`。Sketch、TypeMouse、Launcher、入力、BLE、boardのアプリ動作は今回変更していない。既存の高速Sketch・長押し固定/キー反転などは基点Integrationから保持。

## USB Shell操作

本体画面のShellではなく、既存のUSB診断Shellのコマンド。

```text
display test display0 writer-lut status
display test display0 writer-lut baseline
display test display0 writer-lut candidate_d
display test display0 writer-lut disable
display test display0 writer-lut trace
```

- `baseline`：Writerの以後のfastest更新をAへ。`disable`は同義。
- `candidate_d`：以後のWriter fastestを毎回Dへ。累積回数では拒否しないが、fault状態では拒否する。
- モード変更は既存の更新境界を待ち、変更だけではパネル更新しない。現在表示済みの黒さを遡って戻す操作でもない。
- `status`：モード、D試行・物理更新・成功/失敗、Writer A/OTP・no-op等の診断値。`/6`やcap表示はない。`trace`は直近32件の更新情報。
- NVSへは保存しない。明示Aは同一Driver稼働中に維持し、再起動後は標準Dへ戻る。

## Host検証

既存主要19実行ファイル＋Sketch Python 9件：PASS。Writer Driver/Async/Shell 3実行ファイルと既存Waveform/Input/HID/BLE 11実行ファイル：PASS。さらに実Driver＋実ExecutorのLUT復帰失敗専用プロセス：PASS。

| 検証 | 結果 |
|---|---|
| 標準D、物理更新1/6/7/20/21回目 | D、上限なし |
| 初回cold custom fastest | D、既存`0x22=0xCC` |
| Aへの明示切替、Dへの再切替 | PASS |
| 他アプリ発生元・置換された保留要求 | A |
| 非同期連続D | 23回目までD、間引きなし |
| 実Executorの更新中モード切替 | Baseline復帰まで待機、LUT取り違えなし |
| RAM/Shadow、retain/swapモデル、R0/R2 | PASS、Dirty転送を維持 |
| no-op・要求集約・アプリ終了境界 | PASS、余計な物理更新なし |
| LUT/同期/BUSY/復帰失敗 | 安全停止、保留更新継続・無条件再有効化なし |
| カウンタ99/999/MAX付近、飽和後 | D継続、trace循環も継続 |

HostのRAMモデルPASSは、実Controller内部の全挙動や長期の光学品質の保証ではない。生ログ・コマンド・終了値は同梱証跡を参照。

## Build・成果物

ESP-IDF 5.5.5 / GCC 14.2.0、`m5paper_mono` / `writerdeck` / `ota`。Compile/link PASS、約254.6秒。今回実際に完了したビルドログのwarning/errorは0/0。

D/SPI20/Dirtyを環境変数で明示有効化せず、PaperMono標準値から有効になることをbuild selectionとDriver/Writer/Executor/Shellのコンパイル定義で確認した。`HYBRID_TRIAL=1`は既存対応モード保持のためで、今回Dの対象はfastestのみ。`WAVEFORM_TRIAL=0`、`SKETCH_LATENCY_TRIAL=0`。旧 `WRITER_D_TRIAL` マクロは使わない。一部内部名にtrialが残るが、累積上限のあるTrial動作ではない。

Application：`E:\ESP32\M5PapserMono-SolarOS\.research\writer-d-integration\artifacts\firmware-app-only.bin`

```text
サイズ: 3,459,904 B
SHA-256: 6e3c6a02b701bf5b5b41a02c36e767ce7dbd4efdda338223c1f7f93f689551a1
OTA枠: 7,340,032 B / 残り 3,880,128 B
```

manifestとbuild-selectionは証跡に保存。参照用partition tableはota_0=`0x10000`、ota_1=`0x710000`、各`0x700000`だが、**接続していない実機の起動slot・書込み先を確定する根拠にはしない**。factory image・partition tableは書込み対象にしない。

## 今後のApplication-only Flash・Rollback

接続と新たなFlash承認後、既存手順を再利用して以下を行う。今回は実行していない。

1. 未保存編集・他セッション競合なしを確認。現物のポート、チップ/MAC、16MB容量、USB Shell、実行/起動slotを確認する。
2. 現行Application・partition table・otadataを読取り、必要なバックアップを保存。現物partition/OTA情報から対象Application offsetと容量を確定する。現在のApplicationをrollback用としてSHAとともに保存する。
3. 上記新ApplicationのSHA・board/flavor/layoutを再照合。対象Applicationだけを書き、読戻しSHAを照合する。bootloader/PT/NVS/FAT/otadata/PHY/非対象slotを変更しない。書込み後・再起動前の保護領域一致を確認する（再起動後はSDKの正常なNVS更新があり得る）。
4. 再起動しUSB Shellと通常画面を確認。Writerを開き、statusがD、通常文章を継続入力して6回を超えてもDが解除されないこと、黒さ・背景・入力応答をユーザーが評価する。固定画像の追加試験や4K撮影は必須にしない。
5. Rollbackが必要なら、その時点で確定した対象Applicationへ保存済みApplicationのみを戻し、読戻し・保護領域・通常起動を確認する。全Flashバックアップを不用意に戻して文書やbondを巻き戻さない。

既知の過去の実用版も保持：`.research/dirty-sketch-integration/artifacts-typemouse/firmware-app-only.bin`、SHA `278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6`。これは現在実機の内容の断定ではなく、既存の復元候補。実書込み時は新鮮な現行バックアップを優先する。旧Flash helperの旧Firmware用SHAガードをそのまま流用して実行しない。

## 保護確認・残る課題

元から存在した29 Worktreeを事前記録し、Integrationのみ承認されたfast-forwardを実施。他28 WorktreeのBranch/HEAD/status/変更中ファイルSHA不変を確認。ルートの `experiment/papermono-sketch-latency-measurement` / `31abd6db216bd94833ae092f6948438f8f1e4256` と未追跡`.vscode/`も維持。既存バックアップは上書きしない。

今回のソース統合は実機を書き換えない。USB未接続期間の実機のD使用回数・現在のモードは推測しない。新Firmwareでの連続Dと黒保持・背景品質は未実機確認。実効DCバランス・残留電荷・長期寿命は未解明であり、今回Host PASSで解決したとは報告しない。ユーザーが許容した短い一時白化を除去する追加開発や恣意的な回数上限の再導入は行っていない。
