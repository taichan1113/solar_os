# PaperMono SSD1677 Candidate F（弱黒化1F）実装報告

2026-10-22 JST。**実装・Host検証・ESP32-S3 Buildを完了。実機Flash、USB/COMアクセス、実機試験、光学評価は未実施。実機試験手順書も作成していない。**

## A. 実装概要

実装Commit：**`70e1c0ffeaf4d4e9e616d47a28ee3e5aaf0386cc`**。

Candidate Fの110B配列 `ssd1677_trial_black_hold_f_lut` を追加した。Baseline Aとの実差分は **offset 1の`00→C0`の1byteだけ**。変更可能と指定されたLUT0先頭2byteの範囲内であり、offset 0はAと同じ`00`、offset 2～109は全byte一致する。

| Phase | Frames | FのSource選択 |
|---|---:|---|
| 0A / 0B / 0C / 0D | 各2 | VSS |
| 1A | 1 | VSH2、公称+5V |
| 1B | 1 | VSS |

Fは`00 C0`、0V×8F→+5V×1F→0V×1F、計10F。公称Source電圧時間積は+5 V·Fであり、電荷量・画素電界・実効DCバランスの測定値ではない。1Fだから安全とは判断しない。

A／D／E1の110B配列は基点Commitと全byte一致。FのLUT1～4、TP/RP/FR、VGH、VSH1/+16V、VSH2/+5V、VSL/−15V、DCVCOM/−1.2VはAと同一。[LUT照合記録](evidence/candidate-f-1f-20261022/lut-checksums.json)

変更ファイルは次の18件。報告書・検証記録は別Commitに分離する。

| ファイル | 変更内容 |
|---|---|
| `src/drivers/epd_ssd1677_reference.inc` | F配列、実送信LUTの選択、既存Baseline復元条件にFを追加 |
| `src/drivers/epd_ssd1677_writer_d.h` | Fのselected/used、status、Trace、区間集計の状態とカウンタ |
| `src/drivers/epd_ssd1677_writer_d.inc` | 明示選択、Sketch適用、F専用計上、既存error契約への接続 |
| `src/drivers/epd_ssd1677_trial_stats.inc` | 区間集計でFを別計上 |
| `src/drivers/epd_ssd1677.c` | Frame開始時の`used_f`クリア1行のみ |
| `src/shell/solar_os_shell_writer_lut.inc` | `lut 5`、Fのstatus/Trace、選択成功の1行応答 |
| `src/shell/solar_os_shell_commands.c` | 既存helpにcandidate_fを追加 |
| `src/apps/solar_os_shell.c` | builtin helpの5:F表示 |
| `doc/manual/commands.md` | 既存manualのlut項目を更新 |
| `tests/host/candidate_f_cases.inc` | LUTデコード、source別適用、計上、Sleep/Wake、fault検証 |
| `tests/host/ram_path_lut_cases.inc` | A/D/E1/F×rows/full、cold、R0/R2の実送信検証 |
| `tests/host/writer_d_driver_test.c` | Fを含む実Driverのpayload/命令列比較、追加ケース呼出し |
| `tests/host/writer_d_async_test.c` | F／F RAM切替／F復元失敗を実Asyncで検証 |
| `tests/host/writer_d_panel_fixture.c` | 実送信がF配列か、Baselineへ戻ったかを捕捉 |
| `tests/host/writer_d_shell_test.c` | 5つの選択、1行応答、F表示、RAMとの独立性 |
| `tests/host/Makefile` | 追加fixtureの依存関係 |
| `tests/host/run_candidate_f.sh` | 既存回帰＋FケースのHost実行入口 |
| `.gitattributes` | 新しいsh入口のLF指定 |

Appの描画処理、Display Service/Executor、SPI設定、Booster、BUSY待ち、OTP、Sleep/Wake実装、RAM転送・Shadow処理は変更していない。pixel-budget、mask、Fastwave builder、balance_idle、新しいpacing、wait、強制refresh、追加activationも導入していない。

## B. Shell仕様

| コマンド | 通常App | Sketch | 選択成功時 |
|---|---|---|---|
| `lut 1` | A | A | `LUT: A` |
| `lut 2` | D | A | `LUT: D` |
| `lut 3` | D | D | `LUT: D` |
| `lut 4` | E1 | E1 | `LUT: E1` |
| `lut 5` | F | F | `LUT: F` |

`lut`／`lut status`は既存の詳細診断を表示し、F選択時は **`mode=candidate_f scope=all-apps`** を表示する。Traceも`mode=candidate_f`、F_attempts/F0x20/F_success/F_errorと区間F集計を追加。既存D/E1のカウンタは維持し、FをD/E1/A/OTPへ誤分類しない。

数値選択の成功出力は全5モードとも1行にした。番号の意味は変更していない。失敗時は短いerror、詳細情報はstatus／既存の長い診断コマンドへ分ける。builtin・manual登録、既存`writer-lut` aliasも維持。

`ram rows`／`ram full`／`ram status`の実装は変更していない。F選択後のrows↔fullでFを維持し、Aへフォールバックしないことを実SPI捕捉とAsyncで確認した。

Fは明示選択するRAM内状態。NVSと`/.shell/startup`は変更しない。**再起動／Driver再作成後は通常App=D、Sketch=A、RAM=rows-dual**。Sleep/Wakeは再起動とは異なり、従来のE1等と同様にRAM内の明示選択を保持する。Wake直後の既存OTP baselineを経てcustom fastestへ戻る。未選択ならSleep/Wake後も従来のD／Sketch A方針を維持する。

## C. Driver整合性

処理は既存の直列経路にFを追加しただけである。

1. Shellの`lut 5`→`epd_ssd1677_writer_lut(...,"candidate_f",...)`。既存READ barrierが進行中のBUSY・post-sync・LUT復元まで待つ。
2. 有効なfastest/tile/SPI20/rows-dual backendで`f_selected`を設定。他候補・baselineを選ぶとF選択を解除。
3. 実行Frameが変更ありの場合、既存条件で`used_f`を決め、F専用attemptを記録。SketchもF対象。無変更Frameは既存no-opで更新を発行しない。
4. 既存RAM準備→Fの105Bを`0x32`へ送信→従来の電圧・`0x21/22/20`→BUSY待ち。変更Frameの画像activationは1回。
5. 従来どおり両RAMを同期し、**Baseline Aの105Bを復元**。この復元で追加`0x20`は発行しない。
6. 全成功後にShadow昇格、F successを記録。LUT送信・BUSY・RAM転送・post-sync・復元失敗は既存fault latchへ入り、Shadow成功昇格・自動再試行を行わない。

既存Display Asyncは無変更。Pending Frameは破棄せず、実行開始時の選択を使う。F→Dの切替後、PendingのSketchがA例外へ戻ること、D→FでPendingのSketchがFを実送信することを確認した。

根拠コード：[F配列・実送信](https://github.com/taichan1113/solar_os/blob/70e1c0ffeaf4d4e9e616d47a28ee3e5aaf0386cc/src/drivers/epd_ssd1677_reference.inc#L60)、[選択・計上・barrier](https://github.com/taichan1113/solar_os/blob/70e1c0ffeaf4d4e9e616d47a28ee3e5aaf0386cc/src/drivers/epd_ssd1677_writer_d.inc#L30)、[Shell](https://github.com/taichan1113/solar_os/blob/70e1c0ffeaf4d4e9e616d47a28ee3e5aaf0386cc/src/shell/solar_os_shell_writer_lut.inc#L136)。

## D. Host検証

**PASS**。入口は`tests/host/run_candidate_f.sh`。WSLのC11 `-Wall -Wextra -Werror`を使用し、既存Driver/SPI捕捉・Controller RAMモデル・実pthread Async fixtureを再利用した。

| 項目 | 結果 |
|---|---|
| LUT | F=`00 C0`、VSS8F/+5V1F/VSS1F、10F、Source積+5 V·F、残108B一致 |
| 既存A/D/E1 | 基点配列と110B一致。実送信と既存選択意味も回帰PASS |
| 8条件 | A/D/E1/F×rows/full。105B実送信、電圧・RAM画像・命令順序・1 activationを検証 |
| 座標・RAMモデル | R0/R2、保持／交換の両仮定、範囲外固定画素、同期後両面・Shadow一致 |
| 転送量 | warm rowsは1変更行で300B、warm full144,000B、cold/未同期192,000B。LUT等を除く画像payload |
| Frame完了 | no-opのactivation 0、変更時1、BUSY後post-sync、Baseline復元、成功後のみShadow昇格 |
| 選択・既定 | FからA/D/E1/D-Sketchへ切替。Dへ戻すとSketch A例外が復活。Driver再作成後F=false |
| Sleep/Wake・OTP | 既存の選択保持とOTP基準再構築、未選択時の通常既定、full/partialでF非使用 |
| Async | BUSY/復元中のF↔D/AとF中のrows↔full。Pending画像・origin保持、追加activationなし |
| 失敗 | LUT/BUSY/RAM write/post-sync、FのBaseline復元失敗。fault latch、Pending停止、Shadow不昇格、再試行なし |
| 計上 | F専用counter/Trace/区間集計。D/E1/A/OTPとWriter A/OTPへFを混入させない |
| Shell | 本体/USBのHost mockで1～5、status/Trace、RAM独立性、選択setterのSPI発行0 |

全体は**既存46実行ファイル＋Sketchパネルfixture、追加Async 10モード、Python 13件**が成功。Writer・Sketch・Launcher・Shell・TypeMouse・BLEを含む既存回帰入口を維持した。全体所要約85.1秒。

[Hostログ](evidence/candidate-f-1f-20261022/host.log)／[実行記録](evidence/candidate-f-1f-20261022/host-result.json)

追加ケース作成中、RAM失敗注入の閾値が単一転送の末尾に達するだけで失敗を起こさないHost fixture上の前提違いを修正した。製品側のRAM転送・同期処理を変更して解決したものではない。最終の全回帰は修正後のソースで実行し、全件成功。

これらはController内部face挙動の実測や光学PASSではない。実機での高速入力・実画素応答も未確認。

## E. Firmware Build

**PASS**。既存`scripts/build_papermono_standalone_lut.sh`を変更せず、Buildだけ実行した。初回約252.6秒、実装Commit後のclean再Build約11.6秒で成功。両BuildのApplication SHAも一致。

| 項目 | 値 |
|---|---|
| Buildした実装Commit | `70e1c0ffeaf4d4e9e616d47a28ee3e5aaf0386cc`、clean |
| SolarOS | 4.16.1 |
| ESP-IDF | 5.5.5 |
| Toolchain | Xtensa GCC 14.2.0+20260121 |
| PlatformIO env | `esp32_s3_devkitc1_n16r8`、実Board生成結果=`m5paper_mono` / SSD1677 |
| Flavor / Layout | `writerdeck` / `ota` |
| Flags | Writer D=1、Dirty hold=1、SPI20=1、Hybridコンパイル=1、waveform trial=0、Sketch latency=0、Sketch LUT panel=0 |
| 表示経路 | 既存fastest＋tile＋Async、rows-dual既定、full切替維持。Hybridの実使用を有効化する変更なし |
| Application `firmware.bin` | **3,466,448 B** |
| Application SHA-256 | `e11da1b81baf0ff60086d9b61d89a4442bddae58e5eec42bdf4698186caa2bc1` |
| OTA容量 | 各**7,340,032 B**。両slotに収まる |
| Partition | ota_0=`0x10000`、ota_1=`0x710000`、各`0x700000`。Build生成Tableを解析、実機未照合 |
| Partition SHA-256 | `fd976d7c1eaa8f9d0a01ac87a8d05fc1cb1e59d3c5970954b30ad0c3aa12a488` |

Applicationのローカル出力先：`.research/candidate-f-1f/worktree/.pio/build/esp32_s3_devkitc1_n16r8/firmware.bin`（Repository rootから）。binaryはGitへ追加していない。

[Buildログ](evidence/candidate-f-1f-20261022/build.log)／[Build・partition記録](evidence/candidate-f-1f-20261022/build-result.json)／[初回Build](evidence/candidate-f-1f-20261022/build-initial-result.json)

## F. Gitと既存作業の保護

| 項目 | 値 |
|---|---|
| Worktree | `E:\ESP32\M5PapserMono-SolarOS\.research\candidate-f-1f\worktree` |
| Branch | `feature/papermono-candidate-f-1f` |
| 基点 | `feature/papermono-ram-path-lut-trial` / `268895c53d95117a717664b8c1ac00a517b55309`。作業開始時originと一致 |
| 実装Commit | **`70e1c0ffeaf4d4e9e616d47a28ee3e5aaf0386cc`** |
| Push先 | `origin` = `https://github.com/taichan1113/solar_os.git`、同名実験Branch |
| upstream | `https://github.com/nilseuropa/solar_os.git`。merge/pushなし |
| Integration | `integration/papermono-dirty-sketch` / `aa08bbfdfa65c72ada0a0bc7737d36b47a521186`を維持 |

既存**37 Worktree**についてBranch/HEAD、status、未Commit diff、未追跡ファイル内容のhashを前後比較し、全て保持した。親Worktreeの`.vscode/`、前回調査報告書、ChatGPT引継ぎ資料も変更していない。[保護確認](evidence/candidate-f-1f-20261022/worktrees-preserved.json)

実装Commit時・clean再Build時の新Worktreeはclean。報告書と検証記録は追加の文書Commitとし、実装Commitのコードを変更しない。Push後の最終HEAD、originとの一致、最終clean状態は完了返信に示す。Force Push、main/Integrationへのmerge、他Branchのresetは行わない。

## G. 未確認事項

- Fによる黒濃度の維持・回復性能。
- D/E1と比べた一時白化の改善。
- 実機の高速入力時の遅延、BUSY、表示品質。
- 黒画素数への依存性。
- 実機での`lut 5`とRAM切替。
- 単極性補償を反復した場合のDC偏り・残留電荷・長期パネル影響。

**実機は今回アクセスしていないため、現に書き込まれているFirmware・保存文書・BLE bond・startupを変更していない。** Host/Build PASSを光学評価PASSとは表現しない。実機評価の条件・回数・停止条件は本書の対象外とし、後続のユーザーとChatGPTの対話で決める。
