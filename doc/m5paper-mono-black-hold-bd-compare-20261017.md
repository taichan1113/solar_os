# SSD1677 Candidate B/D：同一Firmware比較準備（2026-10-17）

> 準備完了時点の記録。翌日の承認済み単発比較と復元・D-4判定は[実機結果](m5paper-mono-black-hold-bd-results-20261018.md)を参照。

**Phase 1・2完了：B/D比較実装・Host・ESP32-S3ビルドPASS。Phase 3の実機Flash承認待ちで停止。** 今回はCOM接続・port走査・Flash読取り/書込み・EPD試験を行っていない。D-1〜D-5の実機判定はまだ付けない。新Candidate、反復試験、実用Branchへのmerge、pushなし。

## Git・実装

| 項目 | 内容 |
|---|---|
| Branch | `experiment/papermono-black-hold-bd-compare` |
| Worktree | `E:\ESP32\M5PapserMono-SolarOS\.research\black-hold-bd-compare\worktree` |
| 基点 | D実機結果まで含む最新HEAD `be172606ee54c174a0b2601f3002386d9261c60e` |
| 実装Commit | `26a9b87c1da931dbcc6cf7babff53a9b9cde93c0` |
| 既存Worktree | 24個のHEAD/statusと記録済み編集file hashが不変 |
| 実用Integration参照 | `391925abff2d261d1d5856711aa0a1f101ac435a`、今回の実機状態は未読取り |

既存Dを基点に、Bの検証済み110バイト配列を戻して共存させた。Dの故障時テスト、固定時刻、display claim、immutable snapshot、OTP cleanup、R0/R2対応を再利用した。[既存D実装報告](m5paper-mono-black-hold-timing-d-20261017.md)／[前回A/D結果](m5paper-mono-black-hold-timing-d-results-20261017.md)

変更は製品コード5ファイルとHost3ファイル。App/Service、Dirty Rectangleアルゴリズム、SPI、board/partition、build scripts、startupは無変更。

| ファイル | 変更 |
|---|---|
| `src/drivers/epd_ssd1677_waveform_trial.inc` | 既存B/D配列を明示名で共存。バイト列自体は既存hashと完全一致 |
| `src/drivers/epd_ssd1677.h` | Trial限定のBaseline/B/D enum、候補別budget照会、故障latch照会、3つの単調時刻保持 |
| `src/drivers/epd_ssd1677.c` | 候補別のboot-global budget、どちらかの強制測定失敗後は両方を禁止、Baseline復帰時刻。通常の無変更更新省略を維持 |
| `src/drivers/epd_ssd1677_reference.inc` | active候補のLUT選択とLUT送信完了時刻だけ。RAM窓・波形駆動・BUSY待機・Shadow処理の変更なし |
| `src/shell/solar_os_shell_black_hold_single.inc` | USB専用B/D受理、候補別開始budget、失敗後次条件禁止、測定完了後のtrace出力 |
| `tests/host/epd_ssd1677_black_single_test.c` | 実DriverのB/D SPI同一性、RAM/Shadow、両順序・候補別制限・故障を検証 |
| `tests/host/black_single_shell_test.c` | B→D、cancel/abort、BUSY/復帰失敗、時刻・復元を検証 |
| `tests/test_black_hold_single_lut.py` | 実B/D両配列を独立デコード、過去hash・offset0のみの差を検証 |

通常fastestはBaselineのまま。B/Dは専用USB Shellからの単発armだけで使用し、通常の表示設定やNVS/startupへ保存しない。A/C/未知名はこの診断で拒否する。Baselineは共通準備に使用するが、Baseline Aの測定条件は用意しない。

Shellでは候補開始時にその候補の開始budgetを消費する。白確認時cancel・測定前abortでも同じ候補を再実行できない。Driverはarm時に候補別budgetを消費し、cancel/失敗でも返さない。開始前cancelではDriverのspentがfalseでも、Shellの開始budgetで再試行を拒否する。Bの正常終了後にDを明示的に開始できる。どちらかのDriver異常・Baseline復帰失敗後は、未使用候補も拒否。Driverのbudget/failure latchはdisplay再初期化で解除されず、Shellの診断失敗latchもその起動中は残る。自動で次条件を起動する処理はない。

## LUTの一致と独立デコード

| 条件 | LUT0先頭 | 相順序 | 110バイトSHA-256 |
|---|---|---|---|
| Baseline | `00 00` | HOLD | `e8a7abbb2110cb8d941e80cc2869bbe1d0814e1d4a38e5d0cc5173b10f39ce50` |
| B | `8F F0` | −15V×2 → HOLD×2 → ＋5V×6 | `45c5419f6986e3b1a81f8394e386fd5bc627503dcaa6d2c483a9113a63209422` |
| D | `2F F0` | HOLD×2 → −15V×2 → ＋5V×6 | `9be4bea8b9a07fef33834293d25979a9b0f3fc820d3e9912a1fc106532ef1832` |

B/D差分は**offset 0のみ**。全110バイト長、LUT1〜4、TP/RP、FR、末尾Gate/Source/VCOMは一致。両者10フレーム、公称Source時間積`−15×2＋5×6＝0 V・frame`。独立Pythonデコードは既存SSD1677 Rev1.0 §6.6/6.7 Table6-6/Fig6-6の符号化に基づき、先頭2相だけが入れ替わることを確認した。実効DCバランス・寿命・メーカー適合を証明するものではない。[デコード証拠](evidence/black-hold-bd-compare-20261017/lut-decode.json)

## Host結果

| 対象 | 結果 |
|---|---|
| 実Driver/SPI専用 | PASS：R0/R2×retain/swap×B→D/D→B。全command順序・RAM/address/voltage/ctrl/restore payload一致。最初の0x32 payload byte0のみ差。測定0x20各1、0x32各2 |
| RAM・Shadow・通常動作 | PASS：両RAM同期、固定画素class、Shadow一致、通常無変更画像は無送信、次の通常描画はBaseline |
| 単発・故障 | PASS：B/D別budget、重複/同時arm拒否、cancel後非返却、display reset後保持、不正名/enum、LUT送信失敗、BUSY timeout、Snapshot不一致、復帰LUT失敗後は両方拒否。自動再試行/余分な0x20なし |
| Shell 10ケース | PASS：normal、r2、fail-B/D、restore-B/D、abort-B/D、cancel-B/D。次条件の明示開始、白/T1 gate、固定時刻、元設定復帰を検証 |
| 独立LUT Python | 2件PASS |
| 旧Waveform Host | PASS（Hostのみ、実機では無効） |
| 主要Host | 19実行ファイル全PASS |
| Sketch Python | 9件PASS |
| 実機の表示品質・速度 | 未実行 |

主要19件はSSD1677 Driver/Dirty/SPI20/Hybrid、Sketch pipeline/canvas/lifecycle/140%/legacy/projection/trace、Display Async、Writer quality/buffer/files、Launcher touch、TypeMouse engine/lifecycle、Shell launch。詳細と原ログ：[一覧](evidence/black-hold-bd-compare-20261017/host-results.json)／[専用](evidence/black-hold-bd-compare-20261017/host-single-verified.log)／[回帰](evidence/black-hold-bd-compare-20261017/host-regressions.log)。初回のローカル起動スクリプトはCRLFでHost起動前に停止したためLFに修正し、検証済み実行はexit 0。

## ビルド・成果物

| 項目 | 結果 |
|---|---|
| Board / Flavor / Layout | m5paper_mono / writerdeck / ota |
| ESP-IDF / Compiler | 5.5.5 / GCC 14.2.0 |
| ESP32-S3 compile/link | PASS、248.49秒 |
| Applicationサイズ | 3,459,712 B |
| OTA slot / 余裕 | 7,340,032 B / 3,880,320 B |
| Compiler/CMake warning/error | 各0件 |
| Application SHA-256 | `0c31b2ac41aaba98d4d1dee1514f3e787d0297ed063d39eabedccbba2a12d1ab` |

有効：BLACK_HOLD_SINGLE=1、DIRTY_HOLD=1、SPI20=1、HYBRID=1。無効：WAVEFORM=0、SKETCH_LATENCY=0（各`SOLAR_OS_*_TRIAL`）。HYBRIDのコンパイル有効はruntime選択を意味しない。SDK CONFIG定義とpartition binaryはD版と一致。Application binary内のBaseline/B/D全配列とB/D専用文言を確認、C配列とA測定文言は不在。Driver起動値AUTO、既存startupがfastest/tileを選択する設計とrows-dual既定を維持。実効SPIは承認後に現物で確認する。

Application-only：`E:\ESP32\M5PapserMono-SolarOS\.research\black-hold-bd-compare\artifacts\firmware-app-only.bin`。同folderに`manifest.json`と`build-selection.txt`。`partitions-reference-only.bin`は照合用で書込み対象ではない。factory結合imageを配布対象にしていない。独立Worktreeの`.pio/build`で実行し、他Worktreeの成果物を上書きしていない。[manifest](evidence/black-hold-bd-compare-20261017/manifest.json)

## 承認後の限定実機計画（未実施）

1. COM候補を決め打ちせず、識別・16MiB・partition・実行/起動slot・現行Applicationを新規確認。未保存文書と他セッションの競合を確認。現行Applicationと全Flashを新規バックアップし、読戻し可能な元Applicationが確定してからApplicationのみ書く。既知Integration hashを今回の実機hashとみなさない。
2. 各書込みの直前/直後でbootloader、partition、NVS/FAT/otadata/PHY等を照合し、bond・保存文書は巻き戻さない。共有Flash lockと既存の検証済み保護/復元手順を再利用する。
3. 起動/USB Shell、SPI実効20MHz、fastest/tile/rows-dualを確認。Launcher等の表示干渉を止める。startupの変更なし。現在の実機・保護領域・画面は今回未確認。
4. Insta360 Linkの画角/本体位置/露出/Gain/WB/focus/照明をB/Dで固定。原則B→D。可能なら1本の連続動画を優先する。B→cleanup→Dの独立初期化は省略せず、各条件はユーザー確認後に別コマンドで開始する。難しければ同一設定の別動画。自然光を可能な範囲で遮り、温度は測れなければ未測定とする。
5. USBから`display test display0 black-hold-single B`。白画面確認後`g`で、OTP共通画像→Baseline初回custom→完了＋5秒T0→＋10秒B単発→完了＋2秒T1を実行。T1で保持し、観察/録画確認後`g`でcleanup。重大異常時はDへ進まない。
6. 明示確認後、同じFirmware/撮影条件で`display test display0 black-hold-single D`を同手順で1回。Bの結果を受けてDのLUTや待機を変えない。A/C・反復は実行しない。
7. 各条件の診断内0x20は白OTP2＋画像OTP2＋準備custom1＋測定1＋cleanup2＝8回。合計16、Candidate B=1/D=1。通常UI更新は別枠。Baseline復帰は0x32のみで物理更新なし。
8. 正常終了後、直前に新規バックアップした元ApplicationへApplication-onlyで復元。読戻しSHA、保護領域、USB Shell、現在の画面、既存startupを確認する。Trialは通常運用に残さない。

既存capture helperをB/D名とtrace採取だけに合わせ、親folderの`capture_black_single.py`へ**オフラインで準備**した。新しいカメラ制御サービス、実機自動シーケンス、自動再試行は作っていない。

## 光学変動と時刻の扱い

測定前5秒（T0〜測定開始）の近傍白・黒・Cをnative frameで記録し、range、drift、急な段差とT0/局所直前との差を報告する。前回Dのような大きな事前段差があれば比較成立として扱わずD-4候補とする。今回はリアルタイムカメラ検出を実装していない。映像から事後確認し、操作者が事前外乱を認識できた場合は既存`q`/Esc/Ctrl+Cで測定前に中断できる。中断/比較不成立でも候補を自動再実行しない。

同じ位置なら黒四角・固定文字・白基準に共通ROIを使用し、位置がずれたら対応ROIとずれを記録。文字maskは各T0で固定しT1で選び直さない。主値は既存のT0/T1後0/0.5/1秒のC中央値。過渡には前回と同じ局所窓（概算更新完了−0.75〜−0.5秒）のC、raw最小、中心3frame中央値の最小、局所低下、T1への回復を使用し、T0基準も併記する。白時系列、clip、PTS間隔/欠落、位置ずれ、照明段差を確認し、可能なら更新周辺の連続frame contact sheetを作る。

既存の開始/終了/T0/T1時刻に、LUT payload送信完了、既存の0x20呼出直前、BUSY待機終了、Baseline復帰前後の単調時刻を追記。traceのSerial整形はDriver終了後に行い、BUSYループ内へ出力しない。**BUSY立上りエッジは未計測**で、activate時刻をBUSY開始と呼ばない。GPIO ISRや追加EPD同期マーカーなし。30fps/rolling shutter/USB受信/作成日時由来の同期誤差を残し、厳密なLUT相の光学対応や短い白化の不在を断定しない。[時刻契約](evidence/black-hold-bd-compare-20261017/trace-contract.json)

## 未実施・停止位置

B/DのT0/T1、局所谷、光学安定性、目視、実機SPI/BUSY/総時間、D-1〜D-5分類、優先候補、今回のFlash/復元結果はいずれも**未実施・未判定**。各1回の数ms差で高速化を断定しない。反復安定性、実効DC、長期安全性は引き続き未確認。

依頼書§5の「Host検証・ビルド完了後、いったん停止して報告する」「ユーザーの明示承認前に実機Flashへ進まない」に従い、ここで停止する。今後の承認対象はB/D同一FirmwareのApplication-only書込み→B/D各1回→元Application復元まで。新Candidate、短期反復、実用統合へ自動移行しない。
