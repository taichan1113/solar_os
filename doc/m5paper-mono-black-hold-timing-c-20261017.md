# SSD1677 Candidate C・単発A/C Trial準備（2026-10-17）

## 状態

Candidate Cの最小実装・Host検証を完了。実機Flashは依頼書§5の明示承認待ちであり、今回の実機測定・光学評価は未実施。準備済み成果物のハッシュとビルド結果は後掲。旧Candidate Bと実用IntegrationのBranchは変更していない。

| 項目 | 内容 |
|---|---|
| Branch | `experiment/papermono-black-hold-timing-c` |
| Worktree | `E:\ESP32\M5PapserMono-SolarOS\.research\black-hold-timing-c\worktree` |
| Trial基点 | `8f060acae3bbece08934985d85092ace3e2ae53b`（前回結果記録済み） |
| Integration基点 | `391925abff2d261d1d5856711aa0a1f101ac435a` |
| C実装Commit | `10213ae1` |

前回の実機最終状態はIntegrationへ復元済み、Application SHA-256 `278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6`、ユーザーがLauncher正常確認。今回のポート列挙ではCOM1・15・16だけで、COM39は検出されなかった。**現在の実機Application hash・partition・起動slotは今回まだ再照合していない。** 接続・承認後、過去値を決め打ちせず現物を確認する。

## LUTの確認

一次資料：[SSD1677 Rev1.0](https://files.waveshare.com/upload/2/2a/SSD1677_1.0.pdf) §6.6 Table6-6／§6.7 Figure6-6。4フェーズはD7:6→A、D5:4→B、D3:2→C、D1:0→Dの順で読む。

Candidate CのLUT0は `BF 00 00 00 00 00 00 00 00 00`。`BF`は`10 / 11 / 11 / 11`、第2バイト`00`は全フェーズVSS。

| Phase | Frames | Source |
|---|---:|---:|
| 0A | 2 | VSL −15V |
| 0B | 2 | VSH2 ＋5V |
| 0C | 2 | VSH2 ＋5V |
| 0D | 2 | VSH2 ＋5V |
| 1A | 1 | VSS 0V |
| 1B | 1 | VSS 0V |

計10フレーム、VSL 2／VSH2 6／VSS 2。旧Bと各電圧の総フレーム数は一致し、配置だけが異なる。公称Source時間積は`−15×2＋5×6=0 V・frame`。実効DCバランス、残留電荷、寿命、黒化・過渡変化改善を保証するものではない。

**依頼書の差分表現の補足：** Baseline先頭は`00 00`のためCとの差分はoffset **0だけ**。旧Bの`8F F0`との差分はoffset **0・1**。指定波形を保ち、この実際の差分をテストする。LUT1〜4、タイミング、FR、末尾電圧設定は全バイト一致。

## 最小変更と既存機構の再利用

変更ファイルは次の5つのみ。

- `src/drivers/epd_ssd1677_waveform_trial.inc`：専用Candidate配列の先頭`8F F0`を`BF 00`へ変更。Baselineや旧dose/hold配列は無変更。
- `src/shell/solar_os_shell_black_hold_single.inc`：条件名とログをA/Cへ変更。旧B引数は拒否。
- `tests/host/epd_ssd1677_black_single_test.c`：CのSPI payloadとBaselineとの差分を検証。
- `tests/host/black_single_shell_test.c`：既存診断のA/C、Cの再実行拒否、B拒否を検証。
- `tests/test_black_hold_single_lut.py`：独立デコード、旧Bとの駆動量比較、差分範囲を検証。

Driver本体・reference更新シーケンス・電圧値・SPI設定・Dirtyアルゴリズム・Apps／Servicesは基点と同一。新しいShell機構や持続的な波形設定は作らない。

既存`SOLAR_OS_SSD1677_BLACK_HOLD_SINGLE_TRIAL=1`を再利用。通常fastestはBaselineで、専用USB Shellの次の1 SnapshotだけCandidateを選ぶ。1起動につきCは最大1回、失敗・キャンセル・display再初期化でbudgetを返却しない。通常no-op省略も維持する。

BUSY正常完了後、A/CともBaseline LUTを同様に再送し、復帰で`0x20`は追加発行しない。失敗時は選択を解除してDriver faultで継続を拒否し、timeout中に無条件の復元命令を送らない。実レジスタreadbackがないため、異常時のController内LUT状態までは確定しない。

## Host検証

- 既存主要19実行ファイル＋SketchトレースPython 9件：PASS。Dirty、Sketch、Writer、TypeMouse、Launcher、Shell等。
- 実Driver＋SPI、retain/swap両モデル、R0/R2：PASS。A/CのRAM内容、0x24/0x26、Shadow、Dirty整合性、03/04/2C、22/20が一致し、差分は最初の0x32 payloadのoffset 0だけ。
- 各測定の`0x20`は1回。`0x32`は候補設定とBaseline復帰の計2回。更新後の通常描画はBaseline、無変更通常描画はno-op。
- 単発制限、エラー・BUSY失敗・古いSnapshotの拒否、cancel／display再初期化でもbudget非返却：PASS。
- 実U8g2・共通固定黒パターンのShell試験：R0、R2、BUSY失敗の3シナリオPASS。旧B拒否、C再実行拒否もPASS。
- Python独立デコード：110バイト、Baseline SHA維持、BF00、指定6フェーズ、10フレーム、公称時間積0、旧Bと電圧別フレーム総数一致、LUT1〜4・FR・電圧バイト一致：PASS。
- 既存waveform Trial回帰：PASS。

ログはWorktree親の`host-regressions.log`／`host-single.log`。最初のHost実行用ローカルscriptはCRLFにより最後のPython module名へCRが混入して失敗した。scriptをLFへ直した再実行は終了コード0。製品コードの不具合ではない。Controller内部状態の実機成立をHostだけで保証しない。

## Build・成果物

ESP32-S3 compile/link PASS（290.56秒）、ESP-IDF 5.5.5、Xtensa GCC 14.2.0。ログの`warning:`／`error:`は0件。旧BのビルドとSDK config定義も一致した。

- Board／Flavor／Layout：`m5paper_mono`／`writerdeck`／`ota`。
- 有効：`BLACK_HOLD_SINGLE_TRIAL=1`、`DIRTY_HOLD_TRIAL=1`、`SPI20_TRIAL=1`、`HYBRID_TRIAL=1`。旧`WAVEFORM_TRIAL=0`、Sketch計測はOFF。
- Hybridのコンパイル有効はruntime選択を意味しない。試験はfastest＋tile、20MHzで実施する。
- Application：3,457,728 B（OTA枠7,340,032 B内）。
- Application SHA-256：`79fda106499056524f499dbf80a05b27dcce3e573b6fbdbfffa6b5afe5e5a6cb`。
- 全110バイトCandidate SHA-256：`45d4fae3d47114dd1aa8c9f8be1b7354430550262163797603e89ea1ad0a0cb0`。
- Baseline SHA-256：`e8a7abbb2110cb8d941e80cc2869bbe1d0814e1d4a38e5d0cc5173b10f39ce50`（従来と一致）。
- 成果物：`E:\ESP32\M5PapserMono-SolarOS\.research\black-hold-timing-c\artifacts\firmware-app-only.bin`。factory／combined imageを試験書込みに使用しない。
- PartitionバイナリはIntegrationの参照バイナリと全バイト一致。現物の起動slotは承認後に別途確認する。
- 実効マクロは`compile_commands.json`／`build-selection.txt`で照合済み。マニフェストは[manifest.json](evidence/black-hold-timing-c-20261017/manifest.json)。
- `audit_candidate.py`でApps／Services／Board／Driver本体・reference更新処理の無変更と、Cの差分範囲を確認。

再ビルドは前回と同じPlatformIO環境・フラグを明示し、今回のWorktree内で`python -m platformio run -e esp32_s3_devkitc1_n16r8`を使用する。`.pio`出力先は他Worktreeと独立。

書込み・capture・復元用の既存ローカルhelperを今回の親ディレクトリへ複製し、保存先・Application hash・サイズ・Commitと条件名Cだけを更新した。**構文確認のみで、USBアクセスやFlashは実行していない。**

## 承認後の単発手順（未実施）

依頼書§5「Host検証・Buildが完了した段階で結果を報告し、実機Flashの明示承認を受けること」により、ここで承認を得る。初期化を含め前回のApplication-only・rollback手順を再利用する。

1. USB接続、未保存編集、Flash競合、カメラ準備を確認。デバイスMAC／容量／partition／実行・起動slotを読み、現行Applicationの正当性と退避イメージを照合する。全Flashも新規バックアップ。
2. Applicationだけを書き込み読戻しSHAを照合。bootloader／partition／NVS／FAT／otadata／PHYを各書込み直前・直後で比較する。startupや文書・bondを巻き戻さない。
3. foreground Shellを確保しLauncherの表示割込みを避ける。`display test display0 black-hold-single A` と `... C` を各1回、operator gateで実行。
4. 各条件はOTP白→OTP共通画像→Baseline初回custom→T0→同一画像の温間単発→BUSY後2秒→T1→OTP cleanup。診断内は各8回の0x20（OTP各2回×3工程＋custom初回1＋測定1）、計16回。Cのactivationはそのうち1回のみ。通常UIの起動・復帰更新は別枠。
5. 白画面・T0・T1は目視確認で進める。カメラの黒つぶれを避け、高FPSが可能なら使用。A/C間と各T0/T1間で位置・露出・WB・焦点を固定し、動画から静止フレームも抽出する。ユーザーは撮影設定を調整可能と回答。
6. BUSY timeout、更新失敗、全面反転、強いちらつき、著しい白化、背景汚染、状態／電源／熱異常では停止・状態保存。軽微な過渡変化は省略せず記録し、完了済み単発を無効とは扱わない。自動再試行・追加cleanup・B再実行・C反復はしない。
7. A/C終了後、試験直前のIntegration Applicationだけを戻して読戻し・保護領域・Launcher・USB Shell・startupを確認する。

## 光学評価の予定と未確認事項

原動画内で固定黒四角・文字と近傍白の同じROIを追跡し、`(白−黒)/白`を計算する。カメラ位置が変われば実画像でROIを選び直し、同一条件内で固定する。T0／T1の安定区間は複数フレームで確認し、更新近傍はnative PTSで時系列化する。

黒四角・文字それぞれについて、T0、T1、更新中の最小値、T0からの低下量、最小値からT1への回復量、隣接白の変動、黒のclip率を記録する。単独の極端フレームだけで判定しない。カメラ／Controllerの同期精度が足りなければ特定LUT位相に対応づけない。

旧Bは[前回実機報告](m5paper-mono-black-hold-single-hardware-20261017.md)と元動画を参考にするが、撮影条件・表示履歴が異なるため、今回との絶対値比較で優劣を断定しない。今回のA/C内比較を優先する。

**光学判定・実機性能・反復試験の推奨可否は未判定。** Host PASSから黒化や過渡薄まり抑制を成功扱いしない。実機承認後の単発測定が完了した段階で、依頼書の4分類に従って判断する。
