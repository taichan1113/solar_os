# PaperMono Type/Mouse Phase 1B-2 — Raw Touch Frame実機診断

2026-10-14。実装・Host検証・compile/link・application-only flashとT0〜T4の測定を完了。
**判定はCONDITIONAL。** 主要な0/1/2点系列とID追跡は成立したが、
無接触中のcount=15を2件観測し、その発生源は未確定。
正常離指のUPは未報告slotに現れ、正常な報告slot内のUPは今回観測していない。
新Frame経路への切替は行っていない。独立レビュー後に次段階を判断する。

## A. 実装・Git状態

- Branch: `experiment/papermono-type-mouse-phase1b2`
- 分岐元: Phase 1B-1最新HEAD `6c544febc636cedfca32297eb4b802bff1961df9`
- 初期実装: `686bb7387f70f2c232b15167d6b9df9d11310cc6`
- USB出力修正: `215c180c`
- Host段階の報告: `297afd3e`。本報告・実機証跡は後続commitへ収録。
- Worktree: `E:\ESP32\M5PapserMono-SolarOS\.research\type-mouse-phase1b2\worktree`
- 元Sketch branchは変更せず、既存未追跡 `.vscode/` を保持。
  Phase 1A/1B-1 worktreeも変更していない。

変更ファイル:

- `src/services/solar_os_ft6336.c`、新規 `solar_os_ft6336_raw.{h,inc}`：
  既存Workerの診断切替、PSRAM journal、集計・回収API。
- `src/shell/solar_os_shell_touchraw.c`、`solar_os_shell_commands.h`、
  `src/apps/solar_os_shell.c`：USB専用コマンド。
- `packages/solar_os_packages.toml`、`doc/manual/commands.md`：package/manual登録。
- `tests/host/ft6336_raw_test.c`、`ft6336_raw_shell_test.c`、
  `ft6336_lifecycle_test.c`、`touch_stubs/freertos/FreeRTOS.h`、
  `Makefile`、`.gitattributes`、`run_touch_phase1b2.sh`：Host検証。
- `tests/hil/analyze_touchraw.py`、`tests/test_touchraw_analysis.py`：
  dump解析・検証。gzip証跡も直接読み込める。
- 本報告と `doc/evidence/type-mouse-phase1b2/`：全Raw、解析JSON、SHA256 manifest。

### 診断入口と所有権

```text
touchraw start
touchraw status
touchraw stop
touchraw dump
touchraw clear
```

USB Shell `cdc0`のclaim owner・呼出元context・handle有効性を照合する。
EPD上のShellや他のportは拒否する。touchは既にenableされている必要がある。
既存journalがある場合のstartは拒否し、明示的なclearを要求する。
clearしても離指待ちgateは維持する。

既存FT6336 Workerが唯一の取得担当。通常は6バイト、診断中は既存
`ft6336_read_frame()`による13バイトburstを1 pollに1回だけ行う。
同じpollに旧Readerを重ねず、新しいtask/jobは追加していない。
start時にそのsourceだけをnot-readyにし、既存Pointer quiesceでPRESSを解放する。
quiesce失敗時はreadyと通常状態を戻して開始失敗とする。
診断中はPointer/new Touch Frameを生成・配送しない。

終了後は6バイトReaderの正常count=0までnot-readyを維持する。
指が残る場合・Read失敗時にPRESSを再開せず、全離指を確認した次の通常pollから復帰。
WAIT_RELEASEはsource停止/detach/suspendを跨いで保持する。
切替のための電源/reset/I2C再初期化は行わない。

| 終了条件 | 処理 |
|---|---|
| stop | Workerで停止要求を消費し、全離指待ち |
| 30秒上限 | monotonic deadlineで停止、journal保持 |
| USB/Shell切断 | 接続・claim・開始時handle tokenの失効で停止 |
| I2C失敗 | 各失敗を保存。3回連続で次のpollから停止 |
| journal満杯 | droppedを計上し、次のpollから停止 |
| source停止/detach/suspend | 既存停止・joinと協調、journal保持 |

dumpは停止中のみ。dump lease中はclear/startを拒否し、出力失敗でもleaseを解放する。
emitted/stored/droppedとsequenceにより回収欠落を検出する。
失敗サンプルのゼロrawは欠測であり、実機count=0として集計しない。
journalは再起動まで保持する。今回は全Rawを回収してGitにも保存した。

## B. Host / Firmware検証

### Hostと回帰

```text
wsl -d Ubuntu-24.04 -- sh /mnt/e/ESP32/M5PapserMono-SolarOS/.research/type-mouse-phase1b2/worktree/tests/host/run_touch_phase1b2.sh
wsl -d Ubuntu-24.04 -- sh /mnt/e/ESP32/M5PapserMono-SolarOS/.research/type-mouse-phase1b2/worktree/tests/host/run_touch_phase1b2.sh --sanitize
```

10実行形式、名前付き89ケースと既存3suiteがPASS、FAIL 0。
ASan/UBSanでも同じ10実行形式がPASS、診断なし。
runner内でPhase 1B-1の8実行形式、その中に含まれるPhase 1Aの4実行形式を再実行した。

| 対象 | 結果 |
|---|---|
| Phase 1A/1B-1 | 66ケース＋Input/Sketch projection/FT lifecycle suite PASS |
| Raw Worker/lifecycle | 15ケース PASS |
| Raw USB Shell | 8ケース PASS |
| Sketch通常・140%・legacy・Canvas PNG・trace core | 追加5実行形式 PASS |

開始/二重開始/確保失敗/quiesce失敗、6/13バイト排他、離指gate、失敗非再利用、
timeout、USB切断/Shell claim失効、overflow、dump lease/出力失敗/index範囲、
clear、suspend/detach後の復帰を検証。出力修正後にもHost/Sanitizerを再実行してPASS。

Python関連回帰は117件中116 PASS / 1既知FAIL。

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
`static uint32_t sketch_canvas_version` に対するstatic-state policyで、Phase 1B-1と同一。
当該Sketch sourceに今回の差分はない。新しい回帰FAILはない。
Windows Pythonのboard testはcurses import不可だったため、WSLで33件PASSを確認した。

### Buildと出力修正

PlatformIO `esp32_s3_devkitc1_n16r8`、ESP-IDF 5.5.5、GCC 14.2.0。
専用worktree自身の `.pio/build` を使用。

```text
SOLAR_OS_BOARD=m5paper_mono
SOLAR_OS_FLAVOR=writerdeck
SOLAR_OS_LAYOUT=ota
SOLAR_OS_SKETCH_LATENCY_TRIAL=0
SOLAR_OS_SSD1677_WAVEFORM_TRIAL=0
```

compile/link成功、compiler warning/errorなし。
初回configureのmanual登録不足はreference追加で解消し、generator 13テストもPASS。
最初のflash後、既存Shell printfの192バイトbufferが長い行を切ることを確認した。
タッチ測定を始める前に、診断コマンド内で768バイトのchecked formattingと
length-aware writerへ変更し、Host/Sanitizer/buildを再検証して再flashした。
共通Shell/TUI APIは変更していない。Hostのprintf stubもproductionの制限を再現するよう修正した。

| 指標 | Phase 1B-1 | 最終Phase 1B-2 |
|---|---:|---:|
| 内部RAM | 52,992 B | 53,160 B（+168 B） |
| Flash | 3,420,471 B | 3,424,935 B（+4,464 B） |
| application bin実サイズ | — | 3,430,720 B |

ELF上の診断状態: raw_status 136 B、raw_lock 8 B、raw_owner 8 B、
raw_records pointer 4 B、raw_stop_request 4 B。
journalはstart時にEXTERNAL_REQUIREDでPSRAM確保。
実機でも1件48 Bを確認、4,096件 = 196,608 B。内部RAMへfallbackしない。
毎Readのmalloc・ファイル保存・文字列整形・Serial同期出力なし。

```text
Firmware: .pio/build/esp32_s3_devkitc1_n16r8/firmware.bin
SHA256: 819660edb7882d226867c82b7027db0718c91b791d86938bd38a4b02c9fd273f
Application ELF SHA256: a0c82684f62a6f58310699ed0c83274871267d6be2d09c54e1b2c1278b2044e8
```

### Application-only flash

COM39: USB VID=303A/PID=1001、ESP32-S3、MAC `28:84:85:44:79:68`。
USB ShellでRunning/Boot=ota_0、slot 0、0x10000、size 0x700000、state validを確認。
download modeでも実partition tableとbuildの一致、otadata CRC/sequence/stateから
同じslot 0が選択されることを確認してからapplicationだけを書いた。

2回のapplication-only flashはともにreadbackがartifactと完全一致。
bootloader/table・NVS・otadata・PHY・FATを同じbootloader session内で前後比較し、完全一致。
factory結合image・全領域erase・OTA選択変更は使用していない。
書込み前はShellのみが開いており、Writer/SketchのRAM編集は開いていなかった。

旧アプリSHA256は `e5e2ad2604060528fb07982d94a5ce70e2add79d327ca2ba3f4963dcc6e0c0c3`
（以前のWriter burst trialと一致、SolarOS 4.16.1/ESP-IDF 5.5.5）。
現在は上記最終診断アプリが通常モードで起動している。

ローカル証跡:
`.research/host.log`、`sanitize.log`、`sketch-host.log`、`python.log`、
`board-python.log`、`manual-python.log`、`analysis-python.log`、
`host-output-fix.log`、`sanitize-output-fix.log`、`build-output-fix.log`。
flash比較は `.research/flash-20261014-224445/summary.json` と
`.research/flash-20261014-225034/summary.json`。
秘密を含みうるNVS等のバックアップはローカルのみで、Gitに収録していない。

## C. 実機Raw Frame

| ケース | count系列 / 件数 | ID・flag | Decoder結果 | 異常 |
|---|---|---|---|---|
| T0 無接触 | 0:244 | 報告IDなし、未報告slotのDOWNはactive扱いしない | OK 244 | なし |
| T1 1本指 | 0→1→0、0:2369/1:141 | ID0保持、DOWN→CONTACT、UPはcount=0 | OK 2510 | なし |
| T2 2本指（T2R） | 0→2→1→0、0:2403/1:1/2:103/15:1 | ID0,1、slot順入替6回 | OK 2507/INVALID 1 | 無接触中count=15 |
| T3 2→1→0 | 0:2280/1:131/2:98/15:1 | ID0,1→0、slot順入替2回、離したID1のUPは未報告slot | OK 2509/INVALID 1 | 無接触中count=15 |
| T4 移動 | 0→1→0、0:2130/1:380 | ID0保持、DOWN→CONTACT→未報告UP | OK 2510 | 報告点に異常なし |
| T5 近接・交差 | 未実施 | 未実施 | 未実施 | count異常を踏まえて見送り |
| T6 四隅 | 未実施 | 未実施 | 未実施 | 通常Sketchの位置一致を確認し、端の追加測定は省略 |

主要5ケース計10,282 Read、OK 10,280 / INVALID 2 / UNCERTAIN 0。
有効2点Frameは計201件、重複IDなし、正常count=0/1/2の報告ID Fなし。
2点保持中に配列順序だけが変わり、ID0/1と各座標の対応は維持された。
**slot indexを指のidentityとして使ってはいけない**という実測根拠が得られた。

最初のT2は30秒上限まで無接触2,512件のみで、操作を捕捉できなかった。
成功扱いせず、準備確認後にT2Rを1回取り直した。補助T2を含む全12,794件を保存。
T0は2秒静置に制御待ちが加わり、取得区間3.171秒。
T1/T2R/T3/T4は30秒上限で停止。物理操作は各ケース短時間で1回ずつ行った。

### 代表Raw（status + P1六バイト + P2六バイト）

| 状態 | Raw hex | 解釈 |
|---|---|---|
| T0初期無接触 | `00 000000000000 0000000000ff` | count=0、両slot未報告 |
| T1 DOWN | `01 00d901870000 ffffffffffff` | ID0=(217,391)、DOWN |
| T1 CONTACT | `01 80d901870000 ffffffffffff` | ID0保持、CONTACT |
| T1全離指 | `00 40d901870000 ffffffffffff` | ID0のUPが未報告slotに残る |
| T2R DOWN | `02 00c101430000 00f3123e00ff` | ID0=(193,323)、ID1=(243,574) |
| T2R順入替 | `02 80f3123e0000 80c1014300ff` | slot0=ID1、slot1=ID0、両座標保持 |
| T3片方離指 | `01 80a4011e0000 40e7124100ff` | 残るID0=(164,286)、ID1のUPは未報告slot1 |
| T3全離指 | `00 40a4011e0000 ffffffffffff` | 最後のID0のUP、count=0 |
| T2R単発異常 | `ff 40a4115d0000 ffffffffffff` | count=15、INVALID、reasons=0x74 |
| T3単発異常 | `ff 40c101430000 ffffffffffff` | 同じ不正count、INVALID、reasons=0x74 |

未報告slotのID F/RESERVED/座標4095は頻出するが、activeとして解釈していない。
count=0の未使用slotに0x40埋めやゼロの変化もあり、未使用bytesが安定するとは限らない。
T4の有効IDは0、raw X=282..294/Y=151..506、隣接点最大移動10px。
正常count内の報告点に明らかなID/座標誤対応は観測しなかった。

## D. I2C性能・ログ検証

| ケース | Read時間 p50/p95/最大（ms） | 開始間隔 p50/p95/最大（ms） | Logger平均/最大（µs） |
|---|---|---|---|
| T0 | 2.4345 / 2.550 / 65.635 | 11.999 / 12.082 / 76.557 | 9.55 / 225 |
| T1 | 2.442 / 2.513 / 47.186 | 11.999 / 12.034 / 57.098 | 7.92 / 242 |
| T2R | 2.442 / 2.525 / 66.510 | 11.999 / 12.035 / 77.550 | 7.90 / 104 |
| T3 | 2.442 / 2.520 / 77.224 | 11.999 / 12.035 / 88.630 | 7.87 / 103 |
| T4 | 2.442 / 2.517 / 48.774 | 11.999 / 12.030 / 58.292 | 8.06 / 461 |

全ケースのI2C失敗・最大連続失敗・journal dropped/overflowは0。
補助T2にはRead最大80.407ms、開始間隔最大92.063msがあった。
長い待ちの原因は未特定。Read値はReader呼出区間で、バス待ち・スケジューリング・
既存Decoderを含む。logger平均は保存/集計区間の合計/Read数で、全poll overheadではない。
USB接続チェック等はlogger区間に含めない。通常10ms待ちを変更せず、実取得周期は約12ms。

p50は中央値、p95はnearest-rank。全ケース20件以上で不足フラグなし。
解析は全raw/result/reasons/両Contact、sequence、時刻、duration/interval、
emitted/stored/droppedを照合する。失敗をcount=0として再利用しない。
全ケースのdump回収は完了し、全件再解析・SHA256検証も実施した。

```text
python tests/hil/analyze_touchraw.py doc/evidence/type-mouse-phase1b2/T1-dump.log.gz --output T1-analysis.json
```

全Rawのgzip・解析JSON・SHA256 manifestを `doc/evidence/type-mouse-phase1b2/` に収録。
gzipはRawを変更せず圧縮したもの。独立レビューで全件を再解析できる。

13バイトburstはcontroller内部snapshotラッチの保証ではない。
実際にcount=15を2件観測したため、系列全体を「不整合なし」とは言えない。
controller内部更新、I2C信号/取得経路等の発生源は未確定。
有効0/1/2点の追跡結果と、この取得異常を分けて評価する。

## E. Decoder/Coreへの影響

今回、Decoder/Coreの契約を変更していない。
count異常・duplicate・報告slotのID F/RESERVEDはINVALID、
報告slotのUPはUNCERTAIN、未報告slotは保存するがactiveとして扱わない。

正常離指のUPはcountに含まれず未報告slotに現れた。
報告UPはcount=15のINVALID 2件にだけあり、正常UP/UNCERTAIN契約を緩める根拠にはならない。
少数のgestureの結果であり、全条件での保証でもない。

Phase 1B-3への条件・候補:

1. 未報告slotのUP/ID F/RESERVED/残留座標をactive判定から除外する。
2. 正常snapshotのcount減少/ゼロから離指を判断し、報告UPを必須にしない。
3. Raw IDで追跡し、今回の8回のslot入替・2→1→0を回帰fixtureへ追加する。
4. count=15をFrame全体INVALIDとして棄却し、現行CANCEL/全離指gateを維持する。
   発生源とgesture中の取消/復帰条件を切替前にレビューする。
5. 正常count内のID F/duplicate/RESERVED拒否と、報告UPのUNCERTAINは現状を維持する。
6. 近接/交差時のidentity、逆側の指を残す遷移、座標端は今回未確定。

## F. 既存動作・保護領域

実機で終了後mode=IDLE/ready=yesを確認。source capabilityは従来の
absolute,pointer-buttonsのみで、新Frame経路は未有効。
通常Writerの空RAM文書表示、Sketchの新規空Canvasへの1点描画と位置一致をユーザー確認。
描画前後でPointer counterは17→53、最後はRELEASE/buttons=0。
テストCanvasは保存せず閉じ、既存文書を編集・保存していない。
Shellへ戻って線が消え、文字と白背景が正常なこともユーザー確認。

UI140%、primary font=mono/textsize=20、refresh=fastestを維持。
BLEはremembered 1/1を維持。今回キーボードの実再接続試験はしていない。
flash直後・再boot前のNVS/FAT全域一致がbond/Writer/recovery/保存設定保護の根拠。
primary volumeの既存ファイル一覧も確認。SDへ書込みしていない。

FT6336 Reader/Decoder、Input/Session、Writer/Sketch描画、BLE、SSD1677、SPI、
Board/I2C/GPIO/power構成には今回の差分がない。
touchは通常APIで一時enableし、終了後に元のmanual OFFへ戻した。
最終状態はShellのみactive、診断IDLE、touch ready=no、Worker停止・電源OFF。
最後のjournalはRAMにも残り、全ケースのRawは回収済み。

実機の接触残存中stop、診断中suspend、Shell切断時の終了はHost検証のみ。
既存アプリ・通常入力の復帰をそれらの実機試験の代用としてPASSにしていない。

## G. 最終判定

**CONDITIONAL：Raw計測は成立し主要系列を確認したが、
単発不正countの発生源と近接/交差等の一部仕様は未確定。入力切替の承認とはしない。**

T0〜T4、性能・集計・全Raw回収、通常入力/既存アプリ復帰、
application-only保護を完了。T5/T6と上記未確認の実機経路は未実施と記録する。
独立レビューで不正Frameの扱い・全離指gate・ID追跡を確認してから次段階を判断する。
Phase 1B-3、新アプリ、BLE HID、Sketch Phase 2計測には進んでいない。
