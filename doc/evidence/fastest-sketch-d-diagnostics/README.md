# PaperMono Sketch D試験モード・連続更新診断（Phase A）

2026-10-20。対象：M5Paper MONO／ESP32-S3／SSD1677、WriterDeck、ESP-IDF 5.5.5。**メインPCではCOM探索・実機アクセス・Flash・Reset・目視試験を行っていない。** UbuntuサブPCへPhase Bを引き継ぐ。

## A. 変更概要

基点はUbuntu試験資料を含む`e4ae53fc7c9c2ae5d707988714eb5051f8c37f92`。新Branchは`feature/papermono-sketch-d-diagnostics`、専用Worktreeは`.research/sketch-d-diagnostics/worktree`。既存Worktree・未Commit報告書・研究資料・`.vscode/`を保護する。main／IntegrationのMergeはしない。

- `epd_ssd1677_writer_d.h/.inc`：RAM内Sketch D opt-in、固定サイズ区間統計、Traceメタデータを追加。
- `epd_ssd1677_trial_stats.inc`：区間開始／停止・飽和加算・BUSY／2種類の間隔・波形／source／RAM集計。新しいQueue・Mutex・Taskはなし。
- `epd_ssd1677_reference.inc`／`epd_ssd1677.c`：既存のRAM経路・窓・周期同期を記録し、OTPのRAM byte数を補完。SPI送信内容・順序・既存BUSY／復元・Shadow昇格を変えない。
- `solar_os_ssd1677.c`：既存Executor statusを読むcallbackの接続／解除だけ。`solar_os_display_async.c`と描画方式は変更しない。
- `solar_os_shell_writer_lut.inc`／help：新モード・区間CLIとUSB出力を追加。
- Host：実Driver／SPI／RAMモデル／実pthread Executorと既存回帰を拡張。

新規波形なし。A=`00 00`、D=`2F F0`、E1=`3B F0`の110B配列は全byte不変。LUT1〜4、TP/RP/FR、Gate／Source／VCOM、OTP、SPI20、rows-dual、32更新周期のRAM再同期、pacing、Sketchアルゴリズム、Launcher／Touch／Power／Sleep-Wakeは変更していない。

## B. CLI仕様

```text
display test display0 fastest-lut candidate_d_sketch
display test display0 fastest-lut candidate_d
display test display0 fastest-lut baseline
display test display0 fastest-lut candidate_e1
display test display0 fastest-lut trial-start
display test display0 fastest-lut trial-status
display test display0 fastest-lut trial-stop
display test display0 fastest-lut status
display test display0 fastest-lut trace
```

| 選択 | 通常App | Sketch Canvas／UI | status |
|---|---|---|---|
| baseline／disable | A | A | mode=A |
| candidate_d／通常boot | D | A | mode=D、scope=all-apps-except-sketch、sketch_d_trial=0 |
| candidate_e1 | E1 | E1 | mode=E1、scope=all-apps |
| candidate_d_sketch（明示実験のみ） | D | D | mode=D、scope=all-apps、sketch_d_trial=1 |

USB専用・NVS保存なし。既存`writer-lut` aliasを維持。通常MCU再起動／新Driver作成はD／Sketch A。controller cold初期化や通常Sleep/Wakeの既存RAM内選択の扱いは変えていない。

実行中BUSY→post RAM同期→A LUT復元の完了を既存barrierで待ち、Pendingは保持して実行開始時の選択を使う。D-Sketch→通常DでSketch Aが復活する。切替自体で物理更新は発生しない。他refreshは既存方式を尊重し、FASTEST/tile/20MHz/rows-dual等の条件不適合やfailure latchでは実験選択を拒否する。

実更新の確認は`source=sketch mode=D 0x20=1 err=0 restored=1`。`restored=1`は次の物理更新を起こさずAの105B LUTへ戻したことを示し、D-Sketch選択解除ではない。

## C. 診断仕様

詳細な定義は[Ubuntu手順書の区間診断](manual-ubuntu-trial.md#6-区間診断)を参照。

- **区間**：start→stopの集計。startは統計だけをReset、active中の再start／未開始・停止済みstopは拒否。stop後は固定し、次のstartまで保持。累計・Trace・Shadow・表示・周期同期counter・Driver faultはResetしない。
- **粒度**：波形A／D／E1／OTPとsource writer／sketch／otherは実画像Activation数。OTP fullの2回を別々に数える。source other内の個別App名は集計しない。no-op／errorはpresent単位。電源だけの0x20は除外。
- **時刻**：起動後monotonic μs。既存の0x20送信直前／BUSY完了検出を使用。BUSYはソフトウェア観測時間（送信・guard・RTOS粒度込み）。開始→開始、前回完了検出→次開始の両方を区間内隣接Activationだけで集計。最初の区間外gapは除外。失敗時の不確かな完了時間は`timing_missing`へ分類。
- **RAM**：差分`region`と実`ram_window`を分離。path=`full/rows/rect/otp/none`、成功writeのRAM payload byte合計、各pathのpresent件数、周期再同期件数。部分write失敗の途中byte数は計測しない。RAM窓が物理駆動範囲だとは解釈しない。
- **要求／集約**：区間CLI時だけ既存Executor statusをcallbackで取得する。submitted／replaced／cancelled／rejected／writer_coalescedの差分と境界のpendingを表示。受付群と実行群は異なるため、pending境界で要求数とphysicalは一致しない。raw座標数ではない。
- **32件超**：全区間の件数・合計・min/max/平均を固定サイズ保持する。Traceは既存の直近32記録のまま、全時系列を保存しない。
- **番号**：`seq`はno-op／失敗を含むpresent記録番号。Traceの`activation_seq`は電源制御を除いた画像Activationの起動後番号。複数Activationのpresentはfirst,lastを表示。
- **限界**：uint64集計は飽和し`overflow`を表示、以後の合計・平均で比較しない。既存Queueのuint32折返しは検出時`wrapped=1`、2^32件以上の区間には使わない。writer_coalescedは既存の飽和counterのためUINT32_MAX到達時の差分は欠落し得る。未取得callbackは`queue valid=0`。

毎更新の動的確保・大量ログ・ファイル／NVS書込み・新しい同期機構はなし。時刻計測用の新たな待機も追加していない。物理更新後の一定量の算術・固定状態更新のみ。平均の除算や文字列出力、Queue取得はCLI時に行う。ELF型情報をオフラインで照合したESP32 ABIではrecord 232B、summary 320B、既存32件Trace込みstate 8,376B。Driver全体は6,672→8,992B、**追加2,320B/Driver**の固定状態で、履歴を無制限に増やさない。Host ABIではstate 8,384B。ESP32上の実行負荷・速度差は未実測。

区間CLIはBUSY完了を待つが、inputや既存pacingの設計を変えない。stop時のactiveは区間内、後で実行するpendingは区間外。主試験では指を離し最終表示を待ってからstopする。

## D. Host検証

**PASS**。`CJSON_DIR=<ESP-IDF>/components/json/cJSON sh tests/host/run_sketch_d_diagnostics.sh`で再現する。

- A／D／E1／D-Sketch×source writer／sketch／other、全モード間遷移を実SPI捕捉で検証。Sketch D→通常DのA例外復活、R0/R2、retain／swap双方のRAMモデル、payload／電圧／Activation不変・Shadow同期・LUT復元。
- 実ExecutorでBUSY／復元待ちのD-Sketch↔A/E1とPending source保持、実際の選択LUT、区間stopのbarrierとPending cohortを検証。
- 実Driverで40 fastest＋OTP fullの2 Activationを集計し、Trace32件超でも42 Activation・no-op・source・成功RAM byte・full／rows／周期同期が失われないことを検証。
- 既知の合成時刻でBUSY／start-gap／ready-gapのcount・sum・min/maxと最初のgap除外を確認。飽和・Queue折返し・不正start/stop・停止後固定・新規区間・LUT失敗／復元失敗のlatchも確認。
- 既存Launcher 2×3、Files／Reader／Clock Touch、Input、Sleep設定、Writer、Sketch、TypeMouse、BLE等の回帰を含む46 executable target、追加Async 5モード、Python 12テスト。

初回のShell Host検証で、テストが実IDFのエラー番号259を決め打ちしていたため、スタブの`ESP_ERR_INVALID_STATE=0x107`と不一致で失敗した。期待値を定義済み定数へ直して再実行しPASS。Firmwareのエラー制御を変更したものではない。初回ログはローカルに保持する。

## E. Build

**PASS**。Host回帰後の初回Buildは332.41秒で成功し、その実装をCommitした後、同Commitの最終Buildも9.39秒で成功した。両ログにcompiler warning／errorなし。

| 項目 | 結果 |
|---|---|
| Build環境 | PlatformIO `esp32_s3_devkitc1_n16r8`、pioarduino 55.3.312 |
| Board／Flavor／Layout | `m5paper_mono`／`writerdeck`／`ota` |
| ESP-IDF | 5.5.5 |
| Toolchain | `xtensa-esp-elf-gcc.exe (crosstool-NG esp-14.2.0_20260121) 14.2.0` |
| 実装・最終Build Commit | `e0009c3d546fa67e809d1d00bcb50349a43ebfa8` |
| Application | `.pio/build/esp32_s3_devkitc1_n16r8/firmware.bin` |
| Application size | **3,463,552 B** |
| SHA-256 | `478b1b7ebd0d0bf938328a273674c78205eb38791be58860810cab1966f8fab6` |
| OTA slot容量 | 7,340,032 B（7 MiB）、余裕3,876,480 B。消去sector切上げ後も適合 |
| 実効設定 | fastest + tile／20MHz／rows-dual／Display Async、Launcher 2×3／Sketch Async |
| 有効flags | `SOLAR_OS_SSD1677_WRITER_D=1`、`DIRTY_HOLD_TRIAL=1`、`SPI20_TRIAL=1`、`HYBRID_TRIAL=1`（後3つも`SOLAR_OS_SSD1677_` prefix） |
| 無効flags | `SOLAR_OS_SSD1677_WAVEFORM_TRIAL=0`、`SOLAR_OS_SKETCH_LATENCY_TRIAL=0` |

生成Boardヘッダ・実compile引数・link map・Application内のモード文字列を照合した。Sketch高速化とAsyncはBoard設定で有効のまま、旧レイテンシ計測Trialだけを無効にしている。20MHzはBuild設定確認であり、今回は実SPI測定をしていない。LUT A／D／E1のSHAは基点と一致し、App／Async／Board／CMake／SDK設定は不変。Service差分は既存status callback接続だけ。

親PCの保管場所：`.research/sketch-d-diagnostics/artifacts/firmware-app-only.bin`。BinaryはGitへ追加していない。Buildが生成するfactory／bootloader／partition tableはFlash用Application成果物として扱わない。

## F. Git Push

Remote **`fork`**（`https://github.com/taichan1113/solar_os.git`）、Branch **`feature/papermono-sketch-d-diagnostics`**。実装Commit **`e0009c3d546fa67e809d1d00bcb50349a43ebfa8`**を通常Pushし、Remote HEAD一致を確認した。その後の資料CommitはFirmwareソースを変更せず、完了報告に最終Full SHA・Remote一致・clean状態を示す。

既存origin=`https://github.com/nilseuropa/solar_os.git`は不変。main／Integration／既存研究BranchのHEAD、既存34 Worktreeのstatus／未CommitファイルSHAは開始時と一致。親PCの前回ChatGPT向け未Commit報告書もそのまま保持。Force Push・他BranchへのPush・Merge・実機操作なし。

証拠：[最終Buildログ](build.log)／[Build結果](build-result.json)／[Hostログ](host-regression.log)／[Host結果](host-regression-result.json)／[構成・Binary manifest](manifest.json)／[実装Push照合](push-result.json)／[Pushログ](push-source.log)／[Worktree保護](worktrees-preserved.json)／[固定RAMサイズ](ram-footprint.json)／[証拠SHA-256](checksums.json)。

## G. Ubuntuへの引継ぎ

[manual-ubuntu-trial.md](manual-ubuntu-trial.md)にFetch／独立checkout／再Build／fresh backup／実機slot監査／Application-only書込み／起動前読戻し・保護領域全byte照合／Rollback／A-D-E1の最大約20回試験／必要時だけのD連続・間欠比較を記載した。

UbuntuではそのPCで生成したbinaryのSHAを記録し、同じbinaryの読戻しと照合する。以前のota_0 offsetを決め打ちせず、標準PlatformIO Uploadを使わない。前回終了時のWriter／Sketch未保存sessionを改めて確認する。通常終了は`candidate_d`でD／Sketch Aへ戻す。

RAM経路切替機能・強制同一画像更新・自動連続試験・pacingは追加していない。20回上限を周期同期観察のために自動延長しない。Writerの詳細z4/z7比較は次段階。

## H. 未確認事項

Sketch Dの実機適用と黒保持、D/E1白化差、連続描画の黒濃度、更新間隔・RAM経路との相関、LUT2新黒／LUT0保持の寄与、実機BUSY、診断による実機CPU負荷、Touch／Power lifecycle再発、長期的影響、今回のFlash成功は未確認。HostやBuildの成功をこれらの実機PASSとは表現しない。
