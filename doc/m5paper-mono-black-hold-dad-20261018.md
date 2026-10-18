# SSD1677 D→A→D：実装・Host検証・ビルド報告（2026-10-18）

**D→A→Dの準備完了。Gate 1（実機Flash承認）前で停止する。** 依頼書§4・§14に従い、今回の作業は実装・Host検証・ESP32-S3ビルドまで。COM列挙・接続・状態照会・Flash・EPD試験は行っていない。Writer Trialの実装も未着手。

## Git・成果物

| 項目 | 結果 |
|---|---|
| Branch | `experiment/papermono-black-hold-dad` |
| Worktree | `E:\ESP32\M5PapserMono-SolarOS\.research\black-hold-dad\worktree` |
| 基点Commit | `1e11d7fc09b47f2b0d07e328524632b6f5f0ed9d`（D→A→Aの最新結果・証拠を含むHEAD） |
| 製品コード実装Commit | `e5b307cc6515b7a8bbcd3344924f453d50effa47` |
| Board / Flavor / Layout | m5paper_mono / writerdeck / ota |
| Toolchain | ESP-IDF5.5.5 / GCC14.2.0 |
| Application-only | **3,460,720 B** |
| Application SHA-256 | `647534b1231faf8945f0fb15566ab0be1eb21d62715cd612aedcd92533a6ddc0` |
| OTA slot容量／残量 | 7,340,032 B／3,879,312 B（Application binary基準） |
| Build | PASS、278.9秒、Compiler/CMake warning 0・error 0 |
| 既存作業 | 既存27 WorktreeのHEAD・Branch・status・記録対象編集hashが前後不変。元Worktreeの未追跡`.vscode/`も維持 |

Application保存先は`.research/black-hold-dad/artifacts/firmware-app-only.bin`。[manifest](evidence/black-hold-dad-20261018/manifest.json)にSHA・フラグ・partition・各検証結果を記録した。独立Worktreeの`.pio/build/esp32_s3_devkitc1_n16r8`でビルドし、他WorktreeのBuild出力を使っていない。SDK設定とpartition binaryは既存検証版と一致した。

## 最小変更と再利用

製品・Build設定6ファイル、Host関連4ファイルの計10ファイルを変更した。

- `src/drivers/epd_ssd1677.c`：D/A/Dの固定予約順序、合計3・D2・A1の非返却予算。
- `src/drivers/epd_ssd1677.h`：既存固定シーケンスbackendに依存する、既定OFFのDAD Trialフラグ。
- `src/shell/solar_os_shell_black_hold_single.inc`：DAD診断名・ログ名と3回目のD選択。
- `src/shell/solar_os_shell_commands.c`：専用USB診断コマンドの登録。
- `src/CMakeLists.txt`・`scripts/platformio_solaros_flavor.py`：明示Buildフラグの検証・記録。
- `tests/host/Makefile`・`black_single_shell_test.c`：既存fixtureを再利用するDAD targetと3回目LUT期待値。
- 新規`epd_ssd1677_black_dad_test.c`・`black_dad_shell_test.c`：既存D→A→Aテストを基にしたDAD専用検証。

OTP初期化/cleanup、同一画像生成、display claim、immutable submission、BUSY待ち、Baseline LUT復帰、RAM/Shadow同期、各gate、60秒スケジュールとログを再利用した。Driver通常refresh本体、LUT配列、電圧、FR/Timing、Dirty rows-dual、App/Service/Board、Writer/Sketch/TypeMouse、startupを変更していない。新たなpacingや更新方式は追加していない。

有効Trialマクロ（`SOLAR_OS_SSD1677_`接頭辞）：`BLACK_HOLD_DAD_TRIAL=1`、既存backendの`BLACK_HOLD_PRIME_AA_TRIAL=1`・`BLACK_HOLD_REPEAT3_TRIAL=1`・`BLACK_HOLD_SINGLE_TRIAL=1`、`DIRTY_HOLD_TRIAL=1`・`SPI20_TRIAL=1`・`HYBRID_TRIAL=1`、`WAVEFORM_TRIAL=0`。`SOLAR_OS_SKETCH_LATENCY_TRIAL=0`。PRIME_AAは既存gate/計測backendの依存フラグであり、このBinaryでD/A/Aを選択できる意味ではない。Hybridは既存compile機能のみで、専用診断はfastestを指定する。

## 固定シーケンス・送信監査

承認後に使う専用USBコマンド：

```text
display test display0 black-hold-dad
```

候補引数はなく、D→A→D以外への実行時変更は不可。旧D→A→A・repeat3・B/D単発コマンドはこのBinaryに登録しないことをコードとBinary文字列で確認した。通常アプリのfastestはBaselineのまま。

| 段階 | 処理／停止位置 |
|---|---|
| 初期化 | OTP full白→white gate |
| 白確認後 | OTP full共通画像→Baseline初回custom→5秒後T0 gate |
| T0承認後 | Dを1回→Baseline LUT復帰→終了2秒後T1 gate |
| T1承認後 | Aを1回→Baseline状態→終了2秒後T2 gate |
| T2承認後 | Dを1回→Baseline LUT復帰→終了2秒後T3 gate |
| 撮影・確認後 | Controller正常時に予定OTP cleanupを1回→終了 |

C0取得位置は前回と同じ、準備完了＋5秒のT0。最初のDの60秒目標は準備完了を起点とし、2/3回目は直前更新完了を起点とする。依頼書の模式図を理由にC0を60秒へずらしていない。

```text
goal_us    = previous_end_us + 60,000,000
planned_us = max(goal_us, operator_request_us + 5,000,000)
```

各回は必ず明示承認待ち。目標時刻を過ぎても承認なしに進まず、遅延時は実間隔と超過を記録する。タイマーだけの一括継続はない。途中にOTPや初期画像再描画・RAM/Shadow再初期化を挟まない。

Hostの実Driver/SPIでD→A→Dの各コマンド順・RAM payload・電圧・LUT全payloadを監査した。前回D→A→Aとの機能上の違いは3回目のLUT選択とそれに整合する予約予算で、診断名・ログ名・Buildフラグが変わる。D1とD3は同じ105B LUT、A2はBaseline、各回の2個目の`0x32`はBaseline復帰で追加`0x20`なし。LUT0以外のLUT・FR/Timing・Gate/Source/VCOMは一致する。

- Baseline110B SHA：`e8a7abbb2110cb8d941e80cc2869bbe1d0814e1d4a38e5d0cc5173b10f39ce50`
- D110B SHA：`9be4bea8b9a07fef33834293d25979a9b0f3fc820d3e9912a1fc106532ef1832`

各測定`0x20=1`、`0x32=2`、`0x03/04/2C`各1。温間`0x22=0C`、初回custom`CC`は既存どおり。実機の物理LUT readbackは行わず、実施時は送信成功・Baseline復帰ログを確認する。

予定診断内`0x20`：OTP白2＋OTP画像2＋初回custom1＋D/A/D測定3＋cleanup2＝**10**。測定は最大3、D最大2、A最大1。通常UI・boot更新は別集計。4回目のための実機操作は行わない。

予算は予約時に消費し、失敗/cancelで返却しない。予約後cancel・送信/BUSY/復帰失敗でシーケンス全体を停止し、自動継続・自動再試行しない。Controller/Display再初期化でもstatic予算・失敗latchを保持し、Shellも同一起動中1セッションのみ。本体全体の再起動ではRAMガードが失われるため、予算更新目的に再起動しない。NVS/startupへ診断設定を保存しない。

## Host・Build結果

| 検証 | 結果 |
|---|---|
| 新DAD Driver | PASS：D/A/D選択・D2予算・4回目拒否、RAM/Shadow、通常更新Baseline、R0/R2、retain/swap両RAMモデル、周期RAM再同期、各段階cancelとLUT送信/BUSY/復帰/画像不整合の停止 |
| 新DAD Shell | **20ケースPASS**：normal/r2、70秒遅延承認、長い時刻、3段階の失敗/復帰失敗/submit失敗/予約後abort、white/T0/T1/T2停止、再開拒否。各printf256B未満も検証 |
| 旧D→A→A | Driver・Shell20ケースPASS |
| 旧repeat3 | Driver・Shell17ケースPASS |
| 旧B/D単発・LUT | Driver・Shell10ケース、Waveform・独立LUT Python2件PASS |
| 主要回帰 | Driver/SPI/Dirty/Display Async/Writer/Sketch/TypeMouse/Launcher/Shellを含むHost19実行ファイルPASS、Sketch Python9件PASS |
| ESP32-S3 Build | PASS。compile command・build-selection・SDK・partition・Binaryを監査 |

今回、製品またはHostテストの実行失敗はない。テストから生成された新Worktree内の未追跡`.o`だけを除去し、ユーザーの既存ファイルやWorktreeを削除していない。新製品コードCommitを固定してからbuildし、build前後のCommit一致を確認した。

## 実機承認後の計画・未確認事項

**実機未実施なのでDAD-1〜5は未判定。** SPI実効クロック、BUSY・SPI API・総時間、回復量、画質は実機で確認する。今回ビルド設定上はSPI20MHz・fastest＋tile・rows-dualだが、現在の実機状態を新たに照会したという意味ではない。

Gate 1承認後、現物のCOM/チップ/Flash/partition/実行・起動slot/Application SHAを再確認する。新規全FlashとApplicationバックアップを作成し、Application-only Flash・読戻し・保護6領域照合・排他lockを既存手順で実施する。既知の復元参考はIntegration `391925abff2d261d1d5856711aa0a1f101ac435a`、Application SHA `278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6`だが、現在の実機を保証する値として使わない。

同一4K30連続動画でC0〜C3を追跡し、C0でROI・文字maskを固定する。黒四角と文字を別々にdC1/2/3、**特にdC3**とC3−C1を評価する。回復率`(C3−C2)/(C1−C2)`はC1>C2が局所変動に対して明確な場合だけ参考算出する。raw/median3の過渡最小、直前白基準、PTS欠落・clipping・反射・目視を併記し、未校正カメラ・温度未測定・同期限界を残す。

短時間の従来同程度の白化はユーザー許容済みとして記録し、それだけを停止・不採用理由にしない。新たな著しい表示異常、長時間残る白化、強いghost、白背景汚染、Driver/BUSY/復帰faultは停止対象。公称Source時間積0は実効DC・残留電荷・寿命の保証ではなく、この不確実性は変わらない。

Phase 1の実機終了後は予定cleanup、元Applicationだけの復元、読戻し・保護領域・USB Shell/Launcher・目視を確認して結果を報告し、**Gate 2前で再び停止する**。全Flashを巻き戻してユーザーデータを過去へ戻さない。

Writer TrialはGate 2承認後に別Worktreeで初めて実装する。その後のHost/Build→Gate 3 Flash承認→A/D各試験（D物理更新最大6）→元Integration復元→Gate 4採用判断も自動通過しない。今回はWriterへの実験モード・無制限D・新Candidate・実用Branchへのmerge/pushを追加していない。

## 証拠

[Host診断ログ](evidence/black-hold-dad-20261018/host-single-verified.log)、[主要回帰ログ](evidence/black-hold-dad-20261018/host-regressions.log)、[Buildログ](evidence/black-hold-dad-20261018/build.log)、[Build結果](evidence/black-hold-dad-20261018/build-result.json)、[manifest](evidence/black-hold-dad-20261018/manifest.json)、[試験計画](evidence/black-hold-dad-20261018/trial-plan.json)、[Worktree監査](evidence/black-hold-dad-20261018/other-worktrees-after.json)、[証拠SHA一覧](evidence/black-hold-dad-20261018/evidence-index.json)。Firmware・全Flash・元動画はGitへ追加していない。
