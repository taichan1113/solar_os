# PaperMono 表示品質 Baseline / Dose 実機A/B比較

試験日: 2026-10-13 JST。結論: **CONDITIONAL — doseの初回黒化が少し濃い可能性はあるが、共通OTP黒画像からの10回更新では、保持性能の優位性を確認できなかった。速度はfastest級を維持した。** 写真は取得できないため、光学的な濃さはユーザーの目視評価として扱い、コントラスト `C=(W-B)/W` と保持比 `R=C_after/C_initial` は未測定。

## 実装・書き込み

- Branch: `experiment/papermono-baseline-dose-ab`。比較の実装/計測commit: `fd27a0c09d7b607198d584c65723db3775a86eb4`。終了後のShell再描画修正: `8a7df22dc671597089de85ed46256b2f5f192248`。開始点: Task 1報告commit `d8213ca1`。Files修正・UI140%・Reader/Writer zoom拡張を含む現行HEADから分岐した。
- 対象: `m5paper_mono` / `writerdeck` / `SOLAR_OS_SSD1677_WAVEFORM_TRIAL=1`。ESP32-S3、COM39、MAC `28:84:85:44:79:68`。
- Artifact: `build/papermono-baseline-dose-ab-trial/firmware.bin`、3,433,840 bytes、SHA256 `b2190ed453646760fa8db9a8e056e71f0ebb3ae9837387657667a3ac69a05aa7`。boot waveformは既存baselineのまま。
- 最終修正版Artifact: `build/papermono-baseline-dose-ab-restore-trial/firmware.bin`、3,433,840 bytes、SHA256 `72fbf8c20769dd6b248dd2d25b6e6ceb0558a147c239678105c26af98994fb39`。変更は診断終了後の描画復帰だけで、4バッチの画像・LUT・更新列・計測区間を変更していない。以下の4バッチ数値は最初のartifactによる測定として記録する。
- 既存ファームウェアにはbaseline/dose選択と従来診断があるが、共通OTP黒画像から始めるB試験、1更新ごとの停止、固定画素・snapshot完了検証を揃えられなかったため、既存 `display test` にtrial限定の選択肢を追加した。
- 実機flash実施。MAC、現在のapplicationハッシュ、partition table、既存otadataの一致を確認し、`ota_0`（`0x10000`）のapplicationだけを書き込んだ。書き込み後のapplication hash検証成功。NVS/FAT/otadata/phy_initを全領域バックアップし、flash前後の全バイト一致を確認した。partition table・ota_1・bootloaderは書き込んでいない。
- 証跡: `.research/baseline-dose-ab/flash-20261013-002135/summary.json`。既存文書・bond・設定の消去や試験ファイルのdeviceへのアップロードはない。通常操作用のRAM selectorを起動後にdoseへ戻し、各診断の終了時に元のselector・refresh policy・timing設定を復元する。
- 修正版flashも同じguardと全バイト比較で成功。証跡: `flash-20261013-005427/summary.json`。最初のflash前と修正版flash後のFAT/NVSを論理比較し、既存39ファイルの追加/削除なし、変更は既存Shellのコマンド履歴だけ。文書・PNG・保存設定を含む他38ファイルは全バイト一致。NVS 38 keysは同じで、変更は起動時の既存WireGuard時刻予約値だけ。BLE peer/bondの6値は一致。証跡: `logical-audit.json`。flash瞬間の保護領域一致と、通常起動による既存の履歴/予約値更新を区別する。

変更ファイル:

| ファイル | 変更内容 |
| --- | --- |
| `src/shell/solar_os_shell_commands.c` | 既存display testのtrial限定dispatch/help |
| `src/shell/solar_os_shell_quality_ab.inc` | 共通画像、ROI検証、A/B手順、1更新ごとの停止、完了確認・終了処理 |
| `tests/host/quality_ab_test.c`、`tests/host/Makefile` | 実U8g2画像とSSD1677送信payloadのHost検証 |
| `tests/hil/capture_quality_ab.py` | 1バッチだけのUSB capture、確認済みgateに対する1文字制御、fast更新を開始できないexit-only確認 |
| `scripts/analyze_quality_ab.py` | setup/cleanup/Shellを除外した1バッチの完了・速度・invariant集計 |
| `tests/test_quality_ab_analysis.py`、`tests/test_quality_ab_capture.py` | 不完全/異常ログの拒否、serial制御の境界検証 |

baseline/dose LUT、電圧/VCOM/FR、SPI 40MHz、tile変換、async executor、Writer/Sketch、TUI/Terminal、UI/font/zoom、永続設定の実装は変更していない。holdは試験していない。

## 再現手順

USB Shellで次のコマンドを1つずつ実行する。画面上のShellや進捗文字は試験中に描画しない。

```text
display test display0 ab-a baseline
display test display0 ab-a dose
display test display0 ab-b baseline
display test display0 ab-b dose
```

各コマンドは白OTP cleanup後に `AB gate=white` で停止する。白画面を確認して `g` を1文字送る。Aは対象波形の初回黒画像、BはOTP fullの黒画像を表示して再び停止する。初期画像を確認後、各 `g` が印の1更新だけを許可する。Aは初回1+印9、Bは印10、どちらもfast更新上限10で `AB gate=finished` に停止する。終了状態を観察した後に `q` を送り、健康な終了経路で同じ画像のOTP cleanupを実施する。次のバッチへ自動移行しない。

Hostのcapture/controlで、ユーザー確認後の1バッチ内だけ、完了gateを待って1文字ずつ送った。各印更新後の500ms待ちを両波形に共通に設定。4バッチを無人で連続実行せず、30/100回への延長・自動再試行も行わない。BUSY/転送/asyncエラー時は停止し、未完了snapshotをshadowに昇格させない。hardware fault後のcleanup再試行はしない。

### 共通画像

実機U8g2の論理解像度800×480、横持ち・USB-Cが左側面上寄りの現行回転を使用。native bufferは48,000 bytes。4バッチすべての初期画像native FNV1aは `c0b78f97`、ROI metadataも一致した。文字は固定の `default-r-20`、同じ文字列・座標を使用。これは診断画像用のフォントで、Shell/Writerの保存font/zoom設定を変更しない。

| ROI | 論理座標 x,y,w,h | 役割 |
| --- | --- | --- |
| 更新印 | 400,160,16,16 | 白↔黒だけを切替 |
| 近い固定黒 | 320,160,48,48 | 更新印からの距離による変化 |
| 遠い固定黒 | 32,160,48,48 | 同上 |
| 固定白 | 32,216,48,24 | 背景基準 |
| 固定文字 | 32,32,600,64 | `FIXED BLACK 0123456789 AaBb`、`Only the small marker changes` |
| 細線 | 32,240,736,1 | 1px線 |
| 太線 | 32,264,736,3 | 3px線 |
| 枠線 | 8,8,784,464 | 固定境界 |

左右の黒い四角は近距離/遠距離ROIであり、baseline/doseの左右分割比較ではない。Aの最後の印は黒、Bの最後の印は白。この印の有無を消え残りと取り違えない。

初期画像の変更画素数、dirty boundsは以下の実機結果で記録する。HostのR0画像FNV1a `03b17ff8` は再現性確認用であり、回転した実機のnative hashと同一である必要はない。

## 自動検証

| 検証 | 結果 |
| --- | --- |
| Host全体 `make -C tests/host test` | 136 executable runs成功。Files、UI100/140、SSD1677、async等を含む |
| 最終診断Hostテスト | 成功。実U8g2画像・font・clip reset・実ドライバーpayloadを検証 |
| Python全体 | 584 tests、OK、4 skipped |
| firmware build | 成功、最終34.686秒。trial=1、m5paper_mono/writerdeck |
| application-only flashと保護領域比較 | 成功 |
| Shell復帰修正後の確認 | 診断Host・cleanup fault後の再描画禁止、Terminal100/140%のHost、Python診断6 tests成功。ビルド132.710秒で成功 |
| exit-only capture制限 | Python診断7 tests成功。gを拒否し、white gateでqのみ送ることを検証 |

Hostは送信0x26/0x24 planeを独立したscalar座標変換と比較し、固定黒00、固定白11、印以外の変更0、印の変更256画素を確認する。4バッチ各10回、途中abort、メモリ不足、未対応geometry/clock/hold拒否、BUSY faultで後続更新/cleanupを行わないこと、shadow昇格とRAM設定復元を検証した。実機ではcompleted generation、driver shadowと描画bufferの一致、snapshot解放も毎更新確認する。SSD1677 RAMの電気的なreadbackや光学測定ではない。

全体Host実行後の最終変更（画像clip reset/identity追加）は最終診断HostとPython全体を再実行済み。既存productionコードに変更なし。ログは `.research/baseline-dose-ab/host-all.log`、`host-ab-final.log`、`python-all-final.log`、`build-ready.log`。

Shell復帰修正の追加ログは `host-restore.log`、`host-terminal-restore.log`、`python-restore.log`、`build-restore.log`。変更した診断終了経路とその利用APIに絞って確認し、4バッチを繰り返して計測値を混ぜていない。

## 実機結果

### 完了・画素検証

| バッチ | 初期黒画像 | 波形 | fast / cold / warm | 印更新 / invariant数 | 完了・cleanup |
| --- | --- | --- | --- | --- | --- |
| A1 | 白→fastest黒化 | baseline | 10 / 1 / 9 | 9 | 成功 |
| A2 | 白→fastest黒化 | dose | 10 / 1 / 9 | 9 | 成功 |
| B1 | 共通OTP full黒画像 | baseline | 10 / 1 / 9 | 10 | 成功 |
| B2 | 共通OTP full黒画像 | dose | 10 / 1 / 9 | 10 | 成功 |

全40 fast更新はerr=0、generation=completed、完了shadowとbuffer一致。全38印更新で固定黒12,337画素の00、固定白11、印以外の変更0、実変更256画素を確認した。Aの初回黒化の変更は各12,337画素。warm全36更新のcontroller側dirty boundsは `384,304,16,16`（論理印座標の現行回転後）で同じ。2 plane転送は毎回96,000 bytes、40MHz、tile。印だけを変更しているが、全面2 plane転送の経路を変更していない。

4ログの解析はすべてvalid=true、異常・未完了・回数超過なし。各バッチの開始白画面はユーザーが正常と確認し、終了状態の回答を得るまでcleanupせず停止した。終了cleanupは4回とも正常完了。B1/B2のOTP初期画像は同一で、ユーザーもB2の開始濃度を「B1と同じ程度」と確認した。

### 目視評価

| 評価項目 | baseline | dose | 比較結果 |
| --- | --- | --- | --- |
| A初回黒化 | A1: 汚れ・残像・ちらつきなし | A2: 「A1よりも黒い気がしますが気のせいかも」、汚れ・残像・ちらつきなし | doseが少し濃い可能性。確実な差とは判断しない |
| A合計10回後 | 黒四角2箇所、固定文字・細線とも「濃さはあまり変わらず」 | 「やや薄くなったような気がします。10回更新だとあまり変化はわかりにくい」 | doseの保持優位を裏づける結果ではない |
| B初期濃度 | 共通OTP画像の開始状態を確認 | 「B1と同じ程度」 | 目視で同程度の初期黒から比較できた |
| B10回後 | 「目立った変化は無し」 | 「目立った変化は無し」 | 両方とも目立つ低下なし。doseの保持優位は未確認 |
| 数値の黒濃度・C・R | 未測定 | 未測定 | 写真なし。値を推定・捏造しない |
| 固定文字・細線 / 距離依存 | A1の両黒四角・文字・線は大きな変化なし。B1は目立つ変化なし | A2はわずかな低下かもしれないとの総合印象。B2は目立つ変化なし | 読みやすさの定量比較、距離による優位差は未確認 |
| 白地・消去残り | A1初回/各開始白は正常。B1終了の総合報告は目立つ変化なし | A2初回/終了に汚れ・残像なし。各開始白は正常。B2終了の総合報告は目立つ変化なし | 個別の消え残り量は未測定。Bは印を白へ戻した状態で観察 |
| ちらつき | A1初回はなし | A2初回はなし | 更新列全体についての個別の回答はない。異常申告なしを定量測定や全更新の個別PASSに置き換えない |

A1終了時にユーザーが見た中央左の四角の右側の小さな黒四角は、9回切替後の印の正常な黒状態。消え残りや固定ROI変更ではない。Bは10回切替のため印が白になり、終了観察時には消えている。

### 更新時間

**単位ms。warm各n=9、p50。setup、OTP full/cleanup、終了後のShell描画は除外。** Driver totalはdriver内部のwall timeで、光学的な黒濃度の指標でも、Writerのキー入力から表示完了までの時間でもない。

| バッチ | Driver total p50 | BUSY p50 | 2 plane変換 p50 | SPI p50 | Driver total p95（参考） |
| --- | ---: | ---: | ---: | ---: | ---: |
| A1 baseline | 112.403 | 51.386 | 34.730 | 19.798 | 112.592 |
| A2 dose | 112.517 | 51.547 | 34.666 | 19.796 | 112.770 |
| B1 baseline | 112.409 | 51.493 | 34.691 | 19.794 | 112.537 |
| B2 dose | 112.503 | 51.481 | 34.688 | 19.797 | 112.676 |

Aのdose−baseline差は+0.114ms（+0.10%）、Bは+0.094ms（+0.08%）。この小標本では実用上の速度差を認めず、約110ms級を維持している。p95はnearest-rankでn=9では最大値に等しく、tail latencyの保証に用いない。

各バッチの最初のfast更新はcoldのためwarmに混ぜない。

| バッチ | cold total | cold BUSY | cold変換 | cold SPI | cold実変更画素 / bounds |
| --- | ---: | ---: | ---: | ---: | --- |
| A1 | 453.312 | 133.976 | 35.608 | 19.823 | 12,337 / 8,8,784,464 |
| A2 | 455.532 | 133.923 | 37.446 | 19.981 | 12,337 / 8,8,784,464 |
| B1 | 454.046 | 134.102 | 37.473 | 19.804 | 256 / 384,304,16,16 |
| B2 | 454.055 | 134.161 | 36.861 | 20.403 | 256 / 384,304,16,16 |

全40 fast更新の各frame実測値は[計測CSV](m5paper-mono-baseline-dose-ab-measurements.csv)、ユーザーの目視回答は[観察JSON](m5paper-mono-baseline-dose-ab-observations.json)に保存する。完全なraw serialログ、解析JSON、gate状態、保護領域バックアップは `.research/baseline-dose-ab/` に保持する。4バッチの解析は `A1.analysis.json`、`A2.analysis.json`、`B1.analysis.json`、`B2.analysis.json`。

### 終了後のShell復帰

最初の終了後、OTP cleanupは正常に完了していたが、ユーザーから「テスト時の線やFIXED BLACKなどの固定文字が残っている」との報告があった。これは成功扱いにしていない。診断の直接描画で変わったbufferに対し、Terminalの既存セル描画cacheが有効なままで、通常のversion出力やcleanupコマンドだけでは画面全体が再構築されなかった。

診断の健康な終了時だけ、display claim解放後に既存 `solar_os_sessions_mark_foreground_dirty()` を呼び、保存中のShell内容から全体を再構築する。次の通常画面のOTP cleanupも既存APIで要求する。Terminal/Services側は変更せず、Shell出力履歴・文書・設定を消去しない。exit-cleanupが失敗した場合はdirty要求を行わず、障害後の再描画・再試行を防ぐ。

修正版で白画面直後に中止する終了処理だけを実機確認した。`--exit-only` のcapture制限はgを送信できず、white gateにqだけを送る。実機ログは `AB end err=0 fast_count=0 healthy=1`、exit-cleanup正常、owner解放を確認。4バッチの再実行・追加fast更新はない。ユーザーは終了後のShell文字・白背景を「正常です」と確認した。保存中のRAM dose選択を維持し、boot defaultはbaselineのまま。

証跡: `ExitOnly1.log`、`ExitOnly1.status.json`、`python-exit-only.log`。一度、通常のA/B開始コマンドを使う復帰確認が自動承認レビューで比較の再実行と判断されて拒否されたため、fast更新を開始できないcapture制限とテストを追加して承認された範囲で終了処理のみ確認した。最初の復帰不具合は修正・再確認済み。その他の回帰不具合は自動検証と今回の実機操作では検出していない。Writer/Sketchの追加操作、電源/RTC/BLEの再試験は本フェーズで行っていない。

## 制限と次の判断

写真なし・非盲検・各条件1バッチ・温度未測定の短い試験。目視の初期濃度や保持を数値に換算しない。過去の測定値を今回の値と混ぜない。coldとwarmを分離し、warmは各バッチ9回のp50で比較する。n<20のp95は参考値。

初期濃度差と保持性能差を別々に判断した。A2は少し濃い可能性にとどまり、B1/B2では同じ初期濃度からの保持差が識別できなかった。1バッチずつの順序固定・記憶による比較であり、統計的有意差や再現性を主張しない。両方が10回で良好だったことを、より長い入力履歴でも薄まらない証明にはしない。

今回の結論はCONDITIONAL。速度の維持は実測できたが、品質向上を確定する根拠が足りない。回数を無制限に増やしたり、新規波形を作ったりせず、将来必要なら同じ短いバッチの順序を入れ替えた反復と固定撮影によるC/R測定を別タスクで検討する。短期10回の結果だけでdoseをNO-GOとも正式採用とも判断しない。明確な品質差が確認できなかったため、今回は追加Writer測定・文書作成・保存操作を行っていない。

本試験だけでDCバランス・長期寿命・正式採用の可否は証明できない。GOは追加検証へ進む判断であり、doseのboot default変更や正式採用を意味しない。
