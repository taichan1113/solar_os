# PaperMono Type/Mouse Phase 1B-2 — Raw Touch Frame診断

2026-10-14。実装・Host検証・PaperMono向けcompile/linkまで完了。
ユーザーの「実装・自動検証を先に進める」に従い、実機測定は未実施。
PCで確認できたポートはCOM1/COM15/COM16で、COM39は見えていない。
実機flash、Raw Frame取得、実測による仕様確定は次の段階に残る。

## A. 実装・Git状態

- Branch: `experiment/papermono-type-mouse-phase1b2`
- 分岐元: Phase 1B-1最新HEAD `6c544febc636cedfca32297eb4b802bff1961df9`
- 実装commit: `686bb7387f70f2c232b15167d6b9df9d11310cc6`
- Worktree: `E:\ESP32\M5PapserMono-SolarOS\.research\type-mouse-phase1b2\worktree`
- 元Sketchブランチは `experiment/papermono-sketch-latency-measurement` のまま。
  元worktreeの既存未追跡 `.vscode/` は保持。Phase 1A/1B-1 worktreeはclean。
- この報告書は実装commitの後続commitに収録する。

変更ファイル:

- `src/services/solar_os_ft6336.c`: 既存Worker内の診断切替、停止/join境界。
- `src/services/solar_os_ft6336_raw.{h,inc}`: 診断状態、PSRAM journal、集計・回収API。
- `src/shell/solar_os_shell_touchraw.c`、`solar_os_shell_commands.h`、
  `src/apps/solar_os_shell.c`: USB Shell限定コマンド。
- `packages/solar_os_packages.toml`: FT6336 packageに診断Shell sourceを追加。
- `doc/manual/commands.md`: 必須のコマンドreference登録。
- `tests/host/ft6336_raw_test.c`、`ft6336_raw_shell_test.c`、
  `ft6336_lifecycle_test.c`、`touch_stubs/freertos/FreeRTOS.h`、
  `Makefile`、`.gitattributes`、`run_touch_phase1b2.sh`: Host検証。
- `tests/hil/analyze_touchraw.py`、`tests/test_touchraw_analysis.py`: dump検証・解析。

### 診断入口

USB Shell `cdc0`から以下を実行する。EPD上のShellや他のportは拒否する。
CDCのclaim ownerと呼出元Shell context、port handleの有効性を照合する。

```text
touchraw clear
touchraw start
touchraw status
touchraw stop
touchraw dump
```

touchは既にenableされている必要がある。診断のstart/stop/clearは
手動enable状態、電源/reset/I2C設定を変更しない。既存journalがある場合の
startは拒否し、明示的なclearを要求する。clear後も離指待ちgateは維持する。

### Worker切替・復帰

既存FT6336 Workerが唯一のI2C取得担当であり、新規task/jobはない。
通常は既存6バイトReader。start要求をWorkerが受けると、そのsourceだけを
not-readyにし、既存Pointer source quiesceでPRESSを解放する。
quiesceに失敗した場合はreadyを戻し、通常経路の状態を保持して開始失敗とする。

診断中は `ft6336_read_frame()` を1 pollに1回だけ呼ぶ。同じpollで6バイトReaderを
呼ばず、Pointerや新Touch Frameを生成・投入しない。通常の10ms待ち時間は維持。
各成功Frameのraw/result/reasonsを保存し、失敗サンプルはerrorと時刻のみ測定値とする。
失敗時のゼロrawは欠測の表現であり、実機のcount=0として集計しない。

終了後は6バイトReaderで正常なcount=0を確認するまでnot-readyのまま待つ。
指が残る場合やI2C失敗時にはPRESSを生成しない。全離指を確認したpollでも
Pointerは生成せず、次の通常pollから再開する。
WAIT_RELEASEはsource停止/detach/suspendを跨いで保持する。

| 終了条件 | 処理 |
|---|---|
| `stop` | Workerで停止要求を消費し、全離指待ちへ |
| 30秒 | monotonic deadlineで自動停止、journal保持 |
| USB/Shell切断 | 接続・claim・開始時handle tokenの失効で停止 |
| I2C失敗 | 各失敗を保存。3回連続で次のpollから停止 |
| journal満杯 | 欠落をカウントし、次のpollから停止 |
| source停止/detach/suspend | 既存Workerの停止・joinと協調して終了。journal保持 |

dumpは停止状態でだけ許可し、dump lease中はclear/startを拒否する。
出力途中のUSBエラーでもleaseを解放する。末尾のemitted/stored/droppedにより
回収欠落を検出できる。journalは再起動で失われるため、その前に回収する。

## B. Host / Firmware検証

### Host

```text
wsl -d Ubuntu-24.04 -- sh /mnt/e/ESP32/M5PapserMono-SolarOS/.research/type-mouse-phase1b2/worktree/tests/host/run_touch_phase1b2.sh
wsl -d Ubuntu-24.04 -- sh /mnt/e/ESP32/M5PapserMono-SolarOS/.research/type-mouse-phase1b2/worktree/tests/host/run_touch_phase1b2.sh --sanitize
```

Phase 1B-2 runnerはPhase 1B-1 runnerを実行し、その中でPhase 1Aの全4実行形式も
再実行する。合計10実行形式、名前付き89ケースと既存3suiteがPASS、FAIL 0。
ASan/UBSanでも同じ10実行形式がPASS、診断なし。

| 対象 | 結果 |
|---|---|
| Phase 1A/1B-1 | 66ケース＋Input/Sketch projection/FT lifecycle suite PASS |
| Raw Worker/lifecycle | 15ケース PASS |
| Raw USB Shell | 8ケース PASS |
| Sketch通常・140%・legacy・Canvas PNG・trace core | 追加5実行形式 PASS |

開始/二重開始/確保失敗/quiesce失敗、通常6/診断13の排他、全離指gate、
失敗サンプル非再利用、timeout、USB切断・Shell claim失効、上限・overflow、
dump lease・出力失敗・範囲外index、clear、suspend・detach後の復帰を検証した。

Python関連回帰は117件中116 PASS / 1 FAIL:

| Suite | 件数 / 結果 |
|---|---|
| board manifests | 33 PASS |
| flavor packages | 42 PASS |
| SSH session policy | 3 PASS |
| input actions | 8 PASS |
| Sketch trace | 8 PASS |
| foreground app state | 3 PASS / 1既知FAIL |
| touchraw analysis | 6 PASS |
| manual generation | 13 PASS |

既知FAILは `src/apps/solar_os_sketch.c:25` の
`static uint32_t sketch_canvas_version` に対するstatic-state policy。
Phase 1B-1と同一で、そのSketch sourceに今回の差分はない。
新しい回帰FAILはない。Windows Pythonではboard testのcurses importが成立せず、
対応するWSL環境で33件を再実行してPASSを確認した。

### ESP32-S3

PlatformIO `esp32_s3_devkitc1_n16r8`、ESP-IDF 5.5.5、GCC 14.2.0。

```text
SOLAR_OS_BOARD=m5paper_mono
SOLAR_OS_FLAVOR=writerdeck
SOLAR_OS_LAYOUT=ota
SOLAR_OS_SKETCH_LATENCY_TRIAL=0
SOLAR_OS_SSD1677_WAVEFORM_TRIAL=0
```

専用worktree自身の `.pio/build` でcompile/link成功。
compiler warning/errorなし。新コマンドのmanual登録不足で最初のconfigureが失敗したが、
reference追加後は成功し、manual generatorの13テストもPASS。

| 指標 | Phase 1B-1 | Phase 1B-2 |
|---|---:|---:|
| 内部RAM（PlatformIO） | 52,992 B | 53,160 B（+168 B） |
| Flash（PlatformIO） | 3,420,471 B | 3,424,835 B（+4,364 B） |
| application binの実サイズ | — | 3,430,608 B |

診断状態のELF symbol: `raw_status` 136 B、`raw_lock` 8 B、`raw_owner` 8 B、
`raw_records` pointer 4 B、`raw_stop_request` 4 B。
journalはEXTERNAL_REQUIRED指定でstart時にPSRAM確保する。
1件48 B × 4,096件 = 196,608 B。内部RAMへのfallbackはしない。
毎Readのmalloc・ファイル保存・文字列整形・Serial同期出力は行わない。

Firmware: `.pio/build/esp32_s3_devkitc1_n16r8/firmware.bin`

```text
SHA256: 5a3ae1ba7ac006965db67bf854a7253598723218a9ff614322571598c00c493f
```

実装commit後にも同じ構成でbuild成功を確認した。生成されたfactory結合imageは
使用していない。**実機flashは未実施**。現在のdevice MAC/partition/OTA選択が
確認できていないので書き込んでいない。

ローカル証跡: worktree `.research/host.log`、`sanitize.log`、`sketch-host.log`、
`python.log`、`board-python.log`、`manual-python.log`、`analysis-python.log`、
`build-final.log`、`build-committed.log`。これらはignoredのローカル記録。

## C. 実機Raw Frame

| ケース | count系列 | ID系列 | Event flag | Decoder結果 | 異常 |
|---|---|---|---|---|---|
| T0 無接触 | 未測定 | 未測定 | 未測定 | 未測定 | 未判定 |
| T1 1本指 | 未測定 | 未測定 | 未測定 | 未測定 | 未判定 |
| T2 2本指 | 未測定 | 未測定 | 未測定 | 未測定 | 未判定 |
| T3 2→1→0 | 未測定 | 未測定 | 未測定 | 未測定 | 未判定 |
| T4 移動 | 未測定 | 未測定 | 未測定 | 未測定 | 未判定 |
| T5 近接・交差 | 未実施 | 未実施 | 未実施 | 未実施 | 未判定 |
| T6 四隅 | 未実施 | 未実施 | 未実施 | 未実施 | 未判定 |

Hostの人工Frameを実機Rawの代用として載せていない。

## D. I2C性能と解析

実機の13バイトRead時間・取得周期・失敗数・logger overhead・overflowは未測定。
Reader呼出前後の時刻を保存するため、Read所要時間には既存ReaderのDecoder処理も
含まれる。loggerの保存/集計区間は別途合計・最大µsを記録する。
この区間値は全poll overheadではなく、USB接続チェック等は含まない。
通常のpoll待ちは10msのままなので、実際の取得周期は測定値から判断する。

停止後dumpを保存して以下で解析する。

```text
python tests/hil/analyze_touchraw.py T1-usb.log --output T1-analysis.json
```

解析は全13バイトを再解釈し、保存済みresult/reasons、両slotの
ID/X/Y/event/reported/validと照合する。sequence、時刻、duration、interval、
末尾件数・dropを検証する。失敗サンプルはFrame集計から除外する。
count分類、報告/未報告slotのflag、UP出現時count、ID F、重複、
隣接2点Frameでのslot順入替、座標範囲、代表遷移を出力する。
p50は中央値、p95はnearest-rank。20件未満には不足の注記を付ける。

13バイトburstはcontroller内部snapshotのラッチ保証を意味しない。
実際の系列をケースごとに確認してから不整合の有無を判断する。

## E. Decoder/Coreへの影響

Decoder/Coreの契約は変更していない。count異常・重複ID・報告slotのID F・
RESERVEDはINVALID、報告slotのUPはUNCERTAIN、未報告slotは保存するが
active contactとして扱わない。実機UP/countの契約は未確定。

Phase 1B-3へ進む前に、正常離指でUPがcountに入るか、count減少時に残るID、
未報告slotのUP/ID F、slot順序、近接時の1点化を実測で確定する必要がある。
UPが正常に報告される場合も、Raw根拠と対応するHostケースを提示してから
Decoder/Coreの変更を検討する。今回の観測前にその変更を行わない。

## F. 既存動作・保護領域

通常Reader復帰、旧1点Pointer、Input source状態、既存Sketch機能はHostでPASS。
USB Shellの診断受付・dumpはHostでPASSだが、実機での通常Shell/Writer/Sketch起動、
タッチ復帰、BLE/settings保持はまだ未確認。

FT6336 Reader/Decoder、Input/SessionのPhase 1B-1コード、Writer/Sketch描画、
BLE、SSD1677、SPI、Board/I2C/power設定に今回の差分はない。
通常sourceは従来のtouch_source_openのままで、新Frame capability/配送は未有効。
NVS/FAT/otadata/PHYへアクセス・書込みしていない。
実機flash前後の保護領域一致は未実施であり、PASSにしていない。

## G. 判定と残作業

**実装/Host/buildは完了。実測によるGO/CONDITIONAL/NO-GO判定は保留。**
実機Rawがないため、Phase 1B-3へのGOではない。

再開時は、接続と未保存編集の有無を確認し、USB Shellで現在firmware・
表示/入力/保存設定を記録する。実機識別、現行partition table、確定済みOTA選択、
現在のapplication slot/offset、imageサイズを確認してからapplicationだけ書き込む。
bootloader/partition/otadata/NVS/FAT/PHYを保護し、同一bootloader session内で
書込み前後の保護領域とapplication readbackを比較する。

正常起動・USB Shell・FT6336を確認後、T0→T1→T2→T3→T4を
1ケースずつ案内し、操作完了を待ってdumpを回収する。T5は前段が正常な場合だけ、
T6は座標確認が必要な場合だけ行う。接触したままの終了は全離指待ちに任せる。
手動touch enable状態を測定前に記録し、終了後に元へ戻す。
診断のためのEPD再描画やfull refresh反復は行わない。

最後に通常6バイトReader・旧Pointer、Shell/Writer/Sketch起動、BLE/bond・
保存データ保持を確認し、実機Rawと結果をこの報告へ追記する。
Phase 1B-3、新アプリ、BLE HID、Sketch Phase 2の計測には進まない。
