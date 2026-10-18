# SSD1677 D→A→A：実装・Host検証・ビルド準備報告（2026-10-18）

**Host/Build完了、Flashの明示承認待ち。** 依頼書§6「Host検証・ビルド完了後に必ず停止する」に従い、ここで停止する。この依頼ではCOM列挙・USB接続・実機状態照会・Flash・EPD更新を一切行っていない。P-1〜P-5は実機測定後の分類であり、今回は未判定。

## Git・作業領域

| 項目 | 結果 |
|---|---|
| Branch | `experiment/papermono-black-hold-d-prime-aa` |
| Worktree | `E:\ESP32\M5PapserMono-SolarOS\.research\black-hold-d-prime-aa\worktree` |
| 基点 | `42aec97cb19db5b920b3d29a8923e3a2e2724737`（前回3回反復の最新結果Commit） |
| 実装Commit | `47921bc7d963d8ddad1a6dfd77f3a50fbeb22c67` |
| 共通祖先 | 前回実装`e1935704df27d6d0e7e5cf25c7d63133c8c74faa`、Integration`391925abff2d261d1d5856711aa0a1f101ac435a`をともに祖先に含む |
| 基点のコード確認 | `e1935704`→`42aec97c`のsrc/scripts/tests差分なし。結果資料を含む最新HEADを基点として既存実装を再利用 |
| 既存作業保護 | 既存26 WorktreeのHEAD・Branch・status・記録対象編集hashを前後照合して一致。元Worktreeは`31abd6db`、未追跡`.vscode/`を維持 |
| ビルド出力 | 新Worktreeの`.pio/build/esp32_s3_devkitc1_n16r8`。他Worktreeの出力を使用しない |

前回の画像・OTP初期化/cleanup、Display claim、immutable submission、RAMモデル、LUT復帰、gateと計測経路を再利用した。Driverの通常refresh本体、LUT配列、電圧、Dirty Rectangle、Apps/Services/Board/partitionは基点から変更していない。実用Branchへのmerge/pushなし。

変更ファイルは11個：`src/drivers/epd_ssd1677.c/.h`、`src/shell/solar_os_shell_black_hold_single.inc`、`solar_os_shell_commands.c`、`src/CMakeLists.txt`、`scripts/platformio_solaros_flavor.py`、`tests/host/Makefile`、既存fixtureの`black_single_shell_test.c`・`epd_ssd1677_black_repeat3_test.c`、新規`epd_ssd1677_black_prime_aa_test.c`・`black_prime_aa_shell_test.c`。

## 固定シーケンスと予算

承認後に使う専用USBコマンド：

```text
display test display0 black-hold-d-prime-aa
```

候補引数を持たず、D→A→Aのみ。旧`black-hold-repeat3 A|D`、旧B/D単発コマンドはこのBinaryに登録しない。通常fastestはBaseline。Dはこの専用診断の予約更新だけに適用する。

| 段階 | 内容と停止位置 |
|---|---|
| 初期化 | OTP full白→`white` gate |
| 白の確認後 | OTP full共通画像→Baseline初回custom→5秒後T0→`T0-next` gate |
| 1回目を承認 | Dを1回だけ予約・送信→Baseline LUT復帰→終了2秒後T1→`T1-next` gate |
| 2回目を承認 | Aを1回だけ予約・送信→Baseline LUT復帰→終了2秒後T2→`T2-next` gate |
| 3回目を承認 | Aを1回だけ予約・送信→Baseline LUT復帰→終了2秒後T3→`T3-cleanup` gate |
| 撮影・確認後 | Controller正常時だけ予定のOTP cleanup→終了 |

T0〜T3は依頼書のC0〜C3に対応する時刻marker。測定の間にOTP full・初期画像の再描画・RAM/Shadow再初期化を挟まない。同じ48,000B snapshotと既存rows-dual経路を継続する。

Driverは起動中の予約をD最大1・A最大2・合計3に固定。最初のA、D再適用、4回目、D後の準備用Baseline再予約を拒否する。Shellも起動中1セッションのみで、途中停止後の再開・再初期化による予算取得を禁止。予約は待機前に消費し、cancel/失敗で返却しない。予約中cancelはそのシーケンス全体をブロックし、Dを飛ばしてAだけ続行できない。失敗時の自動次ステップ・自動再試行なし。

Display/Controller再初期化でも起動中カウンタ・失敗latchは維持する。**本体全体の再起動ではRAMガードが失われる。** 試験を延長する目的で再起動しない。NVS/startupへCandidateや試験設定は保存しない。

`q`/Esc/Ctrl+Cはgate・待機中の停止。正常な途中停止では既存の予定cleanupを1回行う。Driver/BUSY/LUT復帰異常では自動cleanupや再試行をせず、ログ保存・状態確認・適切な復元へ進める構成を維持した。

## LUT・電圧・送信確認

AのLUT0は全ゼロ、Dは`2F F0 00 00 00 00 00 00 00 00`。両110B配列は前回から完全不変。LUT1〜4、Timing/FR、Gate/Source/VCOMも同一。

- Baseline SHA：`e8a7abbb2110cb8d941e80cc2869bbe1d0814e1d4a38e5d0cc5173b10f39ce50`
- D SHA：`9be4bea8b9a07fef33834293d25979a9b0f3fc820d3e9912a1fc106532ef1832`
- DはHOLD×2F→−15V×2F→＋5V×6F、計10F。公称Source時間積0は実効DC・パネル寿命・メーカー適合を証明しない。

実Driver/SPI Host記録で、1回目の最初の`0x32` payload105BがD、復帰がBaseline、2・3回目の最初と復帰の`0x32`がともにBaselineであることを全バイト照合した。各測定`0x20=1`、`0x32=2`。`0x22=0C`、`0x03=17`、`0x04=46 A8 32`、`0x2C=30`は維持。初回customは既存の`0x22=CC`。LUT復帰に追加`0x20`なし。

実機用ログは前回の長い1行を、`BH phase`（count/error）、`BH timing`（時間/RAM）、`BH lut`（要求LUT0・送信成功・Baseline復帰成功）、`BH trace`（各時刻）に分割した。各printfが256B未満で収まることを大きい時刻のHostケースでも確認。Driverの`REF waveform`ログと併用する。**実機での確認は送信成功・選択ログに基づくもので、物理LUT readbackではない**。ログ自身にも`physical_readback=0`を出力する。

## 更新間隔

各更新完了→次の開始を**60秒目標**とし、各回で必ず承認を待つ。最初のDについてもBaseline準備完了を基準に60秒を目標にする。前回の実機操作で約32〜57秒だった承認待ちを考慮し、通常の応答なら目標へ揃えられる余裕を取った。

```text
goal_us    = previous_end_us + 60,000,000
planned_us = max(goal_us, operator_request_us + 5,000,000)
```

承認前には予定時刻を過ぎても更新しない。承認後も最低5秒の動画区間を確保するため、前回完了から55秒を過ぎて承認された場合は60秒を超える。`BH schedule`に前回完了・目標・承認受領・予定時刻、`BH interval`に実測end→startと目標超過を記録する。ESP32のexecutor遅延も実測値へ含める。目標へ揃えるための無承認更新や追加更新はしない。

各更新終了＋2秒の安定markerは維持。承認遅延・温度・実機電源/表示状態は今回未測定で、承認後の実機準備で確認する。温間状態を失った場合は既存Driver guardで更新を拒否し、未知のcold状態で候補を再試行しない。

| 診断内の予定工程 | 0x20 |
|---|---:|
| OTP白 | 2 |
| OTP共通画像 | 2 |
| Baseline初回custom | 1 |
| D→A→A測定 | 3（Dは1） |
| OTP cleanup | 2 |
| 合計 | **10** |

通常Shell/Launcher/bootは別。3回の測定は同一画像の全RAM強制更新で、通常Writerの局所Dirty性能とは別。rows-dualの32回周期RAM再同期はそのままで、RAM同期と`0x20`を別に数える。

## Host・Build結果

| 検証 | 結果 |
|---|---|
| 新Driver | PASS：D/A/A、D1回・A2回・4回目/再初期化/追加準備拒否、各段階cancel/失敗停止、同一RAM・Shadow、R0/R2、保持/面切替両モデル、周期RAM再同期 |
| 新Shell | **20ケースPASS**：normal/r2、70秒承認遅延、長い時刻、各段階BUSY/復帰/submit失敗、予約後cancel、white/T0/T1/T2停止、セッション再開拒否 |
| 既存診断 | B/D Driver・Shell10ケース、3回反復Driver・Shell17ケース、Waveform、独立LUT Python2件PASS |
| 主要回帰 | Host19実行ファイル＋Sketch Python9件PASS（Driver/Dirty/SPI/Hybrid、Display Async、Writer、Sketch、TypeMouse、Launcher/Shell） |
| ESP32-S3 | **PASS**、ESP-IDF5.5.5 / GCC14.2.0、約293.0秒 |
| Compiler/CMake | warning 0／error 0 |

初期の新規テストでは、検証文字列バッファ不足、停止後OTPを測定とみなすfixture条件、中断注入が予約前に発火するfixture条件を修正した。最終関連テストは全PASS。初期3回の失敗ログも別名保存し、最初から全PASSだったとは扱っていない。製品の実機障害ではない。ビルド起動ヘルパー作成時のローカル引用符エラーも修正後、上記の1回の本ビルドが成功した。

本ビルド後の最終確認で、再利用したHost fixtureをMakefile依存へ明記し、専用Driver/Shellの2本を再ビルド・再確認してPASS。Applicationコード・有効フラグ・Binaryは変更していない。このテスト依存追記は報告Commitに含める。

## 成果物

| 項目 | 結果 |
|---|---|
| Board / Flavor / Layout | m5paper_mono / writerdeck / ota |
| Application-only | **3,459,312 B** |
| Application SHA-256 | `3e7f89ddaa64d3adb946ca0f1e13a3b48ae28fe03c875771f8ca914b6fd6649d` |
| OTA 1枠の容量／残量 | 7,340,032 B／3,880,720 B（binary基準） |
| 保存先 | `E:\ESP32\M5PapserMono-SolarOS\.research\black-hold-d-prime-aa\artifacts\firmware-app-only.bin` |
| SDK/partition | 前回3回TrialとSDK CONFIG集合・partition binaryが一致（FreeRTOS1000Hz含む） |

実compile commandとbuild-selectionで確認した`SOLAR_OS_SSD1677_`マクロ：`BLACK_HOLD_PRIME_AA_TRIAL=1`、`BLACK_HOLD_REPEAT3_TRIAL=1`、`BLACK_HOLD_SINGLE_TRIAL=1`、`DIRTY_HOLD_TRIAL=1`、`SPI20_TRIAL=1`、`HYBRID_TRIAL=1`、`WAVEFORM_TRIAL=0`。別接頭辞の`SOLAR_OS_SKETCH_LATENCY_TRIAL=0`。Hybridは既存のコンパイル機能で、診断時はfastestを明示。既存のSketch高速化・TypeMouse等は基点から維持。

新フラグは既定0、Repeat3/Single backend依存を検査。実BinaryにBaseline/D配列とDAA専用コマンド・ログが含まれ、B/C配列と旧A/D選択コマンドがないことを照合済み。Factory binaryはビルドで自動生成されるが配備対象にしない。`partitions-reference-only.bin`は照合用で書込み用ではない。

## 承認後の実機・解析方針

§6の停止点を守り、現時点では実機Flash・Rollback・C0〜C3・BUSY/SPI実測・目視は**未実施**。実機の現在のFirmware/slotを照会していない。直前タスクの復元記録はIntegration `391925abff2d261d1d5856711aa0a1f101ac435a`、参照SHA `278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6`で、ローカル参照binのhashは今回も照合した。現在の実機を保証する値ではない。

承認後に実機・容量・partition・実行/起動slot・未保存編集・競合を再確認し、全Flash/Applicationを新規バックアップする。Application-only Flash、読戻しSHA、NVS/FAT/otadata/PHY/Bootloader/partitionの前後一致を確認する。終了後はその時点の元Applicationだけを復元し、データ領域を過去へ巻き戻さない。実機のCOM39/OTA0/offset0x10000は過去の参考であり未確定。

Insta360 Linkの既存4K30・固定露出/Gain/WB/focus・ROI解析を再利用する。今回の撮影条件は未確認。各T0〜T3の同一ROIとT0固定文字maskで、四角・文字別のC0〜C3、ΔC、dCを取得する。**主指標はdC2=C2−C1、dC3=C3−C2。C3>C0だけでは「初回Dだけで十分」としない。** 前回A/A/A・D/D/Dは別セッションの参考とし、効果量の直接同条件比較はしない。

局所直前C、raw谷、3frame移動中央値谷、安定後C、白基準drift、native PTS gap、clip、反射、更新間隔を記録する。同期・温度未測定と30fpsの限界を明示し、実効DC・寿命をHost PASSや公称時間積0で代用しない。結果はP-1〜P-5を実機後に判定する。毎回D/数回に1回/画質低下時/初回だけの頻度は、この3回だけで確定せず、6回Trial・常用統合へ自動移行しない。

## 証跡

[manifest](evidence/black-hold-d-prime-aa-20261018/manifest.json)、[試験計画](evidence/black-hold-d-prime-aa-20261018/trial-plan.json)、[専用・既存診断Hostログ](evidence/black-hold-d-prime-aa-20261018/host-single-verified.log)、[主要回帰ログ](evidence/black-hold-d-prime-aa-20261018/host-regressions.log)、[buildログ](evidence/black-hold-d-prime-aa-20261018/build.log)、[Worktree監査](evidence/black-hold-d-prime-aa-20261018/other-worktrees-after.json)、[証跡SHA一覧](evidence/black-hold-d-prime-aa-20261018/evidence-index.json)。補助スクリプトはローカル準備用の写しで、実行時の基準ディレクトリは`.research/black-hold-d-prime-aa`。capture adapterは構文検証のみで実行・COM接続していない。Firmware/raw Flash/元動画はGitへ追加していない。
