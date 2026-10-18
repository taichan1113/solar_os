# PaperMono — 共通Candidate D適用・Sketch限定Baseline復旧報告

2026-10-18。全アプリへのD適用と書込みを実施し、実機で判明したSketchの問題に対するユーザー指定の適用除外まで反映した。

## 最終構成

| 対象 | fastestのLUT |
|---|---|
| Shell / Launcher / Writer / TypeMouse / その他 | Candidate D（標準、累積制限・間引きなし） |
| Sketch | **Baseline Aへ例外的に復旧**（UI・Canvas双方） |
| OTP full / cleanupなど別更新モード | 従来どおり |

| 確認 | 結果 |
|---|---|
| Integration | `integration/papermono-dirty-sketch` に反映済み |
| Branch | `feature/papermono-fastest-d-all-apps` |
| 共通D実装Commit | `b163040a82e17af03fed9bcb50e5ca4ed01770e3` |
| Sketch除外Commit（最終ソース） | `57b2f8ff89c2dcb893a1f7d76aba6afe5408f23f` |
| 基点 | `655d61359b3a0d285da2d6e8a949e023168d3d62`（Writer限定D） |
| Host | 主要19実行ファイル＋Sketch Python9件、追加15実行ファイル＋復帰失敗専用プロセス：PASS |
| ESP32-S3 Build | PASS、ESP-IDF5.5.5 / GCC14.2.0、warning/error 0/0 |
| Flash | 最終修正版をApplication-onlyで書込み、読戻しSHA一致 |
| 保護 | bootloader/PT/NVS/otadata/PHY/FAT/非対象OTAの7領域一致（書込み後・再起動前） |
| 目視 | 修正版の表示正常（ユーザー確認） |
| Shell入力・削除 | 最終修正版でも入力・削除・表示正常（ユーザー確認） |
| Sketch等の操作確認 | Sketchの黒線・Shell表示・Launcher復帰すべて正常（ユーザー確認）。TypeMouseの最終修正版でのPC操作は再確認していない |
| 最終実測カウンタ | D 18回・成功18回 / A 54回 / OTP 5回、Driverエラー0。Sketch A trace確認=True、他アプリD復帰trace正常 |

実装Worktree：`E:\ESP32\M5PapserMono-SolarOS\.research\fastest-d-all-apps\worktree`

実用Integration Worktree：`E:\ESP32\M5PapserMono-SolarOS\.research\dirty-sketch-integration\worktree`

## 実機で分かったこと

最初の全アプリD版（Application SHA `769dc0f5d835f70009ea054a021ae1bb5546d7a909a069c1dfd7f53f204375e4`）では、起動表示とShellの文字入力・削除はユーザー確認で正常。D更新は最終的に391回・成功391回・Driverエラー0だったが、**Sketchだけ線が黒くならない**との報告があった。Driverの通信成功と光学品質の成功は別である。

ユーザー指示「今後の研究対象とし、例外的にBaselineへ戻す」に従い、SketchのみDを除外して再ビルド・再Flashした。今回は原因の波形研究やLUT調整には広げていない。初期版のHost・Build・実機ログと失敗申告も証跡の `initial-global/` に保持した。

## 制御の責務と変更

**LUTを決定・送信するのは共通SSD1677 Driver。** Appに波形処理は実装しない。

- Writer限定の `origin.is_writer` 判定を外した。発生元タグのないShell等にもDを適用する。
- 同期U8g2更新も `epd_ssd1677_present_snapshot()` へ通し、非同期ExecutorとLUT復帰・診断・異常停止を共有する。
- Sketchは既存の不変スナップショット発生元に `is_sketch` を付ける。CanvasとUIの送信経路を同じタグ付き経路へ通す。Driverがその更新だけAを選び、全体のDモードは変更しない。
- タグはフレームと一緒にキューへコピーするため、Sketchの処理中に別アプリへ切り替わっても前面アプリの推測でLUTを取り違えない。保留フレームの置換時にもタグが更新される。
- DはBUSYと両面RAM同期後にBaseline LUTへ戻す。復帰による追加`0x20`なし。既存の排他・no-op省略・要求集約を維持し、新しいタスク・キュー・Mutexは追加しない。
- タグを失う同期fallbackへSketchを流さない。通常PaperMonoのタグ付き非同期経路を使用し、未対応・一時的に受理不能な場合はエラーを返して未反映状態を保持する。

主要ソース：`src/drivers/epd_ssd1677_writer_d.inc`、`epd_ssd1677_reference.inc`、`epd_ssd1677.c`、`src/services/solar_os_writer_lut_trial.h`、`solar_os_display.c`、`solar_os_display_async.c`、`solar_os_gfx.c/.h`、`src/apps/solar_os_sketch.c`、`src/shell/solar_os_shell_writer_lut.inc`、`solar_os_shell_commands.c`。変更一覧は証跡 `changed-files.txt` を参照。

`SOLAR_OS_SSD1677_WRITER_D` や一部内部名は互換性のため保持した歴史的名称であり、Writer限定を意味しない。Writer・TypeMouse・Launcherのアプリソースは今回変更していない。Sketchの線分処理・保存形式・描画集約も変更していない。

## 維持した設定と検証

D LUT全110バイトは従来Writer限定版と一致（LUT0=`2F F0`）。Baselineも不変。LUT1〜4、Timing/FR、Gate/Source/VCOM値は変更なし。fastest＋tile、SPI20MHz設定、Dirty rows-dual、Shadow、既存のRAM再同期・更新シーケンス・ペーシングを維持した。

Hostでは非Sketchの1/6/7/20/21回目と実ExecutorのD24回までの継続を確認。Sketch A→他アプリD→Sketch A→Writer Dの実Driver/Executor経路、Sketchの全UI/Canvas送信タグ、既存の線分・最終端・保存/再読込ライフサイクルもPASS。R0/R2、RAM retain/swapモデル、no-op、モード切替、BUSY/LUT/同期/復帰失敗での停止を確認した。LUT保持やDriver状態が不明なまま更新を続けず、自動再試行もしない。

Hostの説明用アプリ名は実機でそのアプリを操作した証拠ではない。実機確認範囲は上表のユーザー回答と実ログに限定する。最初の全アプリ版で起きた光学的不具合をHost PASSで否定しない。

## USB Shell

```text
display test display0 fastest-lut status
display test display0 fastest-lut baseline
display test display0 fastest-lut candidate_d
display test display0 fastest-lut disable
display test display0 fastest-lut trace
```

`baseline` / `disable` は全体をAへ。`candidate_d` はDを再選択するが、**Sketchは引き続きA**。状態表示は `scope=all-apps-except-sketch`、traceは `source=sketch mode=A` と他アプリの `mode=D` を区別する。

旧 `writer-lut` は同じ作用範囲の互換alias。モード変更だけでは物理更新しない。NVS保存なし、再起動後は標準D＋Sketch除外に戻る。

## Firmwareと保護

```text
最終Application: 3,460,512 B
SHA-256: 3374c584f1424829f262aeea61245e5bb5fc24fcd2bdd941619aef00a2004845
OTA枠: 7,340,032 B / 余裕: 3,879,520 B
```

成果物：`E:\ESP32\M5PapserMono-SolarOS\.research\fastest-d-all-apps\artifacts\firmware-app-only.bin`

最終Flashアーカイブ：`E:\ESP32\M5PapserMono-SolarOS\.research\fastest-d-all-apps\archive\sketch-baseline-exception-flash-20261018-232832`

COM39、ESP32-S3、MAC `28:84:85:44:79:68`、16MBを現物確認。実行/起動先とも `ota_0`、`0x10000`、容量`0x700000`。現物partition table・OTA sequence/CRC・slotを照合し、16MB全体と現行Applicationを新規保存後、Applicationだけを書いた。factory image・bootloader・partition table・NVS/FAT/otadata/PHY・非対象OTAは書いていない。

保護7領域は書込み後・再起動前の全バイト一致を確認。SDKが再起動時にPHY calibrationをNVSへ保存するため、通常起動後のNVSまで不変とは主張しない。`/.shell/startup`は保持し、追加設定は書いていない。

## 復元

即時バックアップ `application-rollback.bin` は不具合報告のあった全アプリD初期版（SHA `769dc0f5...`）。これとは別に、既知のWriter限定D版を `application-safe-rollback.bin`（3,459,904 B、SHA `6e3c6a02b701bf5b5b41a02c36e767ce7dbd4efdda338223c1f7f93f689551a1`）として保存した。

専用 `.research/fastest-d-all-apps/restore_writer_guarded.py` は後者の既知版を復元する。未保存編集を保存・終了して競合と現在のslotを確認し、`device_query.py` で新しい `pre-restore-usb.log`（version / sessions / ota status）を採取してから `restore_writer_guarded.py --saved-and-exclusive` を使用する。現在の保存領域を保持してApplicationだけを戻す。OTA選択変更・現行Application不一致・復元試行済みなら停止。全Flashを不用意に戻して文書・bondを巻き戻さない。今回は復元を実行せず、修正版を残した。

## 最終状態・残る課題

**SketchのみBaseline、それ以外はDのIntegrationを実機に残す。** SketchでDを使用した際の「線が黒くならない」現象は未解決で、今後の研究対象。長期の黒保持・背景品質・実効DCバランス・寿命は未評価。短い一時白化は既知の許容動作。

元から存在した30 Worktreeのうち実用Integrationだけを承認されたfast-forwardで更新し、他29のBranch/HEAD/status/変更中ファイルSHAを保持。元Worktreeの `31abd6db` と未追跡`.vscode/`も維持。pushなし。資料と証跡は実装後の独立Commitにする。

生ログ・manifest・保護領域SHA：`doc/evidence/fastest-d-all-apps-20261018/`。全Flashや保存データのバイナリはGitへ含めない。
