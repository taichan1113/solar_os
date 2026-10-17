# SSD1677 Candidate D：実装・Host検証・ビルド（2026-10-17）

> この文書は実装準備完了時点の記録。後日承認を受けた単発A/D実機評価と復元結果は[実機評価報告](m5paper-mono-black-hold-timing-d-results-20261017.md)を参照。

**判定1：Candidate D実装・Host・Build成功。承認と当日の安全確認後、単発実機評価へ進める。** 今回は実機COMへのアクセス、Flash読取り・書込み、A/D表示試験を一切行っていない。黒化作用、過渡白化、実機BUSY／速度、反復安定性は未判定。

## A. Gitと変更範囲

| 項目 | 内容 |
|---|---|
| Branch | `experiment/papermono-black-hold-timing-d` |
| Worktree | `E:\ESP32\M5PapserMono-SolarOS\.research\black-hold-timing-d\worktree` |
| 基点 | 最新B再現性版 `affd31987f81c7249473d64ae3957be199d6ed14` |
| 実装Commit | `e51d5a61db216227a30d514b18aa42c7368e90ab` |
| Integration参照 | `391925abff2d261d1d5856711aa0a1f101ac435a` |
| 基点の固定時刻実装 | `e9f5989671c968f69e5c35893fb4c7573dd7da8a`を継承 |
| 既存Worktree | 23個のHEAD／git statusが不変。記録対象の既存編集ファイルhashも一致 |
| Git状態 | 実装Commit後clean。結果資料を別Commitとして記録する |

既存B/C/Integrationを変更せず専用Worktreeを追加。元Worktreeは`experiment/papermono-sketch-latency-measurement`／`31abd6db`で、既存未追跡`.vscode/`を保持した。merge／pushなし。

製品コードの変更は2ファイルだけ。

- `src/drivers/epd_ssd1677_waveform_trial.inc`：単発候補テーブルの先頭`8F`→`2F`と説明コメント。DとBの110バイト比較ではoffset 0のみ変更。
- `src/shell/solar_os_shell_black_hold_single.inc`：A/B→A/Dの受理条件・usage・phase/end表示のみ。確認gate、表示claim、Snapshot、単発制限、固定時刻、cleanupは継承。

Host変更は`tests/host/epd_ssd1677_black_single_test.c`、`black_single_shell_test.c`、`epd_ssd1677_test.c`、`tests/test_black_hold_single_lut.py`。既存SPI capture stubへ復帰LUT送信の失敗注入6行を追加した。これはHost専用でFirmwareには入らない。

`epd_ssd1677.c`、`epd_ssd1677_reference.inc`、Header、SPI、Dirty Rectangle、Display Executor、Input/BLE Service、Writer、Sketch、TypeMouse、Launcher、board、partition、build scriptsは基点から無変更。

## B. LUTの独立検証

SSD1677 Rev1.0 §6.6/6.7、Table 6-6（p16）、Figure 6-6（p17）を図でも確認した。2bitの`00/01/10/11`はVSS/VSH1/VSL/VSH2、バイトのD7..D0がA/B/C/D相に対応する。[一次資料](https://files.waveshare.com/upload/2/2a/SSD1677_1.0.pdf#page=16)

実装前の独立デコードで`2F F0`を展開して一致を確認してから変更した。実装後も独立Python decoderと実Driver/SPIのHostテストを実行。

| テーブル | LUT0（10バイト） |
|---|---|
| Baseline | `00 00 00 00 00 00 00 00 00 00` |
| B参照（Firmware未搭載） | `8F F0 00 00 00 00 00 00 00 00` |
| D | `2F F0 00 00 00 00 00 00 00 00` |

| 相 | フレーム | B | D |
|---|---:|---:|---:|
| 0A | 2 | VSL −15V | VSS 0V（HOLD） |
| 0B | 2 | VSS 0V | VSL −15V |
| 0C | 2 | VSH2 ＋5V | VSH2 ＋5V |
| 0D | 2 | VSH2 ＋5V | VSH2 ＋5V |
| 1A | 1 | VSH2 ＋5V | VSH2 ＋5V |
| 1B | 1 | VSH2 ＋5V | VSH2 ＋5V |

計10フレーム、電圧別フレーム数が同一、公称Source時間積は`−15×2＋5×6＝0 V・frame`。実効DCバランス、キックバック、パネル応答、長期安全性を証明したものではない。

DはBaselineからoffset 0・1のみ変更、Bからoffset 0のみ変更。LUT1〜4、TP/RP、FR、末尾電圧バイトは完全一致。通常fastestはBaselineで、Dは起動時／NVS／通常アプリから選択されない。

既存テーブルは110バイト（有効byte0..109）。仕様の112バイト表にある予約byte110/111は既存実装どおり使用しない。0x32へ105バイト、残りのGate/Source/VCOM値は既存の個別レジスタ経路を維持している。

Dの110バイトSHA-256：`9be4bea8b9a07fef33834293d25979a9b0f3fc820d3e9912a1fc106532ef1832`。Baseline・Bのhashと独立デコードは[manifest](evidence/black-hold-timing-d-20261017/manifest.json)と[lut-decode](evidence/black-hold-timing-d-20261017/lut-decode.json)。

## C. Host結果

| テスト | 結果／確認事項 |
|---|---|
| `epd_ssd1677_black_single_test` | PASS：実Driver/SPI、A/DのRAM payload・0x24/0x26内容／順序、03/04/2C、22/20の一致、LUT差分、測定0x20各1回 |
| 同上R0/R2×retain/swap | PASS：Controller RAMモデル、Dirty rows-dual、Shadow、同一画像のクラス維持 |
| 同上単発・故障 | PASS：2回目拒否、cancel後再arm拒否、LUT送信失敗、BUSY timeout、変更Snapshot拒否、display再初期化でもbudget返却なし |
| 同上Baseline復帰失敗 | PASS：A/Dとも復帰0x32で失敗を注入。faulted化、Shadow／baseline／controller状態無効化、追加0x20なし、以後の通常present拒否 |
| `black_single_shell_test` normal／r2 | PASS：A/D受理、B/C拒否、非port拒否、A/D各再実行拒否、Baseline復帰、設定復元 |
| 同上fail／abort／cancel | PASS：Dの測定BUSY異常、測定前中断、白確認時cancel後にD再実行不可。異常後の通常再描画なし |
| 固定時刻 | PASS：初回custom完了＋5秒T0、＋10秒測定、測定完了＋2秒T1。途中のT0確認gateなし。fake clockの期限と実要求時刻を検証 |
| `tests.test_black_hold_single_lut` | **2件PASS**：Baseline hash、110バイト、相順序、B/D比較、総フレーム、Source時間積、FR／電圧維持 |
| `epd_ssd1677_waveform_trial_test` | PASS：既存の波形試作回帰。Hostのみ |
| 既存主要Host | **19実行ファイルすべてPASS**、下記 |
| `tests.test_sketch_trace` | **9件PASS** |
| 実機 | **未実行**（今回禁止） |

主要19件：`epd_ssd1677_dirty_hold_test`、`epd_ssd1677_test`、`epd_ssd1677_spi_clock_dirty_20_test`、`epd_ssd1677_hybrid_trial_test`、`sketch_dirty_pipeline_test`、`sketch_canvas_test`、`sketch_lifecycle_test`、`sketch_scale140_lifecycle_test`、`sketch_legacy_lifecycle_test`、`sketch_touch_projection_test`、`sketch_trace_core_test`、`display_async_test`、`writer_quality_test`、`writer_buffer_test`、`writer_files_test`、`launcher_touch_test`、`type_mouse_engine_test`、`type_mouse_lifecycle_test`、`shell_launch_test`。

測定前cancelではShellの試験開始budgetが消費されて再試行を拒否する。Driverの候補budgetはarm時に消費され、失敗・cancel・display resetでも返却しない。この2層を既存のまま利用した。

Host終了コードはいずれも0。最初のPowerShell直接リダイレクトはunittestの正常なstderr出力をNativeCommandErrorとして扱ったため、stdout/stderrを統合して実終了コードを保存するラッパーで確認し直した。製品コードの失敗ではない。検証済みログは親ディレクトリ`host-single-verified.log`／`host-regressions.log`、一覧は[host-results](evidence/black-hold-timing-d-20261017/host-results.json)。

## D. Buildと成果物

| 項目 | 結果 |
|---|---|
| 対象 | `m5paper_mono`／`writerdeck`／`ota`、ESP32-S3、16MB |
| PlatformIO環境 | `esp32_s3_devkitc1_n16r8`、独立Worktreeの`.pio/build` |
| SDK／Compiler | ESP-IDF 5.5.5／GCC 14.2.0 |
| compile/link | PASS、約247.41秒 |
| SDK設定 | BとCONFIG定義が完全一致。Flash16MB、RTOS1000Hz |
| Application-onlyサイズ | **3,458,960 B** |
| 1 OTA slot | 7,340,032 B（7MiB） |
| OTA slot余裕 | **3,881,072 B** |
| Warning/Error | コンパイラ／CMake各0件 |
| Application SHA-256 | `5c8f0944e9b71b134f4aca5584c9e74ae9004cac2e06fab90a4fd10b55343fcd` |

有効マクロはBと同じ：`SOLAR_OS_SSD1677_BLACK_HOLD_SINGLE_TRIAL=1`、`SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL=1`、`SOLAR_OS_SSD1677_SPI20_TRIAL=1`、`SOLAR_OS_SSD1677_HYBRID_TRIAL=1`。`SOLAR_OS_SSD1677_WAVEFORM_TRIAL=0`、`SOLAR_OS_SKETCH_LATENCY_TRIAL=0`。HYBRIDのコンパイル有効はruntimeでの選択を意味しない。

SPI20／rows-dual／tileの既存経路を維持。Driver初期値はAUTOで、既存startupがfastest＋tileを選択する設計も変更していない。試験Shellは20MHz/tile/Baseline等の条件を確認してから専用手順へ入る。今回実機のSPI実効周波数や現在の設定は再確認していない。

Application binary内のD 110バイト・Baseline・`A|D`・`D-single`・固定時刻／中断マーカーを照合。旧B／Cの110バイト波形列と`B-single`は含まれていない。通常fastestのBaseline選択はソース・Hostで確認した。partition binaryはBと完全一致。

実機評価用成果物：

- `E:\ESP32\M5PapserMono-SolarOS\.research\black-hold-timing-d\artifacts\firmware-app-only.bin`
- 同ディレクトリの`manifest.json`／`build-selection.txt`。
- `partitions-reference-only.bin`は照合用。Flash対象ではない。

PlatformIOが通常生成するfactory結合imageは実機候補として採用せず、成果物ディレクトリにはApplication-onlyを分離した。build.logにはWindowsのbuild lockingが利用不可という既存の通知があるが、同Worktreeではこの1ビルドのみを実行。新しいlocking機構は追加していない。

## E. 次回A/Dの手順（未実施）

今回はここで停止する。次回Flashは**別途ユーザー承認後**に、既存Application-only／Rollback手順を再利用する。

1. COM候補を決め打ちせず現物のport・chip・MAC・16MB・partition・実行／起動OTA slotを確認。未保存文書と他セッションの実機使用を確認する。
2. 現在の実用Applicationを読み取り・hash照合し、新規バックアップを取得。書込先と復元元が確定しなければ書き込まない。NVS／FAT／otadata／PHY／BLE bond／保存データを保護し、Application領域以外は書かない。各書込み前後の保護領域一致、Application読戻しを確認する。
3. 以前のIntegration参照は上記Commit、Application SHA `278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6`。そのローカルApplicationのhashは今回確認したが、これを現在の実機hashとみなさず、当日に再照合する。
4. 通常起動・USB Shellを確認し、`sessions`で実際のLauncher／文書／foregroundを確認して表示干渉を防ぐ。既存startupを変更しない。
5. カメラ位置・画角・露出／Gain・WB・focus・FPSを固定して記録。可能なら温度も記録し、未測定なら未測定とする。前回はパネル位置／倍率や自然光の差が残ったため、A/D間の固定を優先する。
6. A動画開始後、USB Shellから`display test display0 black-hold-single A`。白画面確認後に`g`で共通画像／Baseline初回custom／5秒後T0／10秒後単発測定／完了2秒後T1まで自動進行。T1確認・録画終了後の`g`で予定のOTP cleanup。`q`／Esc／Ctrl+Cは既存停止操作。
7. 同じ撮影条件でD動画を新規録画し、`display test display0 black-hold-single D`で同じ工程を1回だけ実施。A/D各1回、B/Cなし。1条件の診断内0x20はOTP3工程×2＋準備1＋測定1＝8回、A/D計16回を予定。通常UIの更新は別枠で記録する。
8. 原動画のfile名・作成／更新日時・録画長・USBログ・hashを照合。前回はWindows作成日時の重複があったため、日時だけで対応を確定しない。元動画を変更しない。
9. 前回と同じT0/T1マーカー後0〜1秒の標本、近傍白基準のC、黒四角と固定文字、native frame最小と3frame移動中央値、低下／回復、白変動、clip、目視、SPI API／BUSY／Driver時間を取得。ROIは動画内で固定し、A/D間でパネル位置が異なるなら対応ROIを設定して差を明記する。
10. A/Dを主比較にし、過去Bは補助比較。30fps／ローリングシャッター／同期誤差のある光学値を、厳密な波形位相や独立反復と扱わない。異常時は停止し自動再試行しない。終了後はバックアップした実用Applicationへ復元して読戻し・保護領域・起動を確認する。

既存capture helperは条件名と完了phase判定だけをA/Dへ変更して親ディレクトリ`capture_black_single.py`に準備済み。構文確認のみで実行していない。新しい診断サービスは追加していない。

## F. 未確認事項と停止位置

実機の黒化、過渡白化の軽減、BUSY／Driver時間、長期DC影響・寿命・反復安定性は**未確認**。実機で良くなるとは事前判定しない。

DのHost・Buildは完了したため、依頼書に従い実機Flash承認待ちで停止する。Phase 3、追加反復、実用Integrationへのmergeは行っていない。本タスクでは実機に接続しておらず、前回確認されたIntegrationからFirmware／startup／保存データを変更していない。
