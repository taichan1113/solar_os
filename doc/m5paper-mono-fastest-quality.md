# PaperMono fastest表示品質 — 波形比較の検証結果

2026-10-10。**2候補を短期比較したが、保持文字の薄まりを改善する波形の正式採用には至らなかった。**
文字書き換え案（dose）は初回の黒・消去残りにわずかな改善の印象があり、
単発Writer入力の中央値は従来163.475msに対して168.511ms（+3.08%）。
固定文字の黒は従来と同程度で、薄まりの改善は実証できていない。
保持黒案（hold）は固定文字が薄くなる結果で除外した。
候補のDCバランス・寿命の根拠が得られていないため、通常版・起動設定は従来fastestを維持する。
候補LUTを除外した通常版をCOM39へ戻し、Shell起動・BLE自動再接続・保存文書保持を確認した。

## 1. 作業ベース・ブランチ・commit

- ベース：`feature/papermono-sketch` / `7bccc265`。
- Sketch実装：`2b702918fde53fcae283c5a09604bd867070eb78`。
- 作業：`experiment/papermono-fastest-quality`。
- 実装commit：`95c56b8e6657e56461cd0d2a1453b28119f13c96`。
- 作業開始時、追跡ファイルの差分はなし。ユーザーの未追跡 `.vscode/` は未変更。
- 比較版は上記実装commitでビルド。通常版はベースHEADに実装差分を適用した
  commit前のビルドで、候補LUTをコンパイル対象から除外する。各SHA256は以下。

## 2. 変更ファイル

| ファイル | 変更 |
|---|---|
| `src/drivers/epd_ssd1677_waveform_trial.inc` | 2候補のLUT。専用defineが有効な場合のみコンパイル |
| `src/drivers/epd_ssd1677_reference.inc` | LUT選択とログへのwaveform追加。転送・BUSY・shadow処理は維持 |
| `src/drivers/epd_ssd1677.h` | 比較用defineとRAM内の選択フィールド |
| `src/drivers/epd_ssd1677.c` | 比較ビルド限定のモードオプション |
| `src/CMakeLists.txt` | opt-in実験ビルドのdefine |
| `scripts/platformio_solaros_flavor.py` | defineの0/1検証とビルド選択stamp。通常版へ戻す際の古いCMake設定流用を防止 |
| `tests/host/epd_ssd1677_test.c`, `tests/host/Makefile` | 通常／比較の実ドライバ・SPI捕捉テスト |
| `tests/hil/papermono_fastest_quality.py`, `tests/hil/README.md` | 一時的な独立診断画面と実行手順 |
| `scripts/analyze_fastest_quality.py` | REF＋既存asyncログのオフライン集計 |
| `tests/test_fastest_quality_analysis.py`, `tests/test_fastest_quality_harness.py` | 不完全ログ排除、統計、診断の回数・中断・解放の検証 |
| 本報告書 | 調査結果、未検証事項、比較・rollback手順 |

Writer、Sketch、BLE、Touch、I2C、Battery、RTC、async executorの実装は変更していない。

## 3. 一次資料

調査日2026-10-10。PDFはテキストだけでなく表を画像化して確認した。

| 資料 | バージョン・参照箇所 | 確認できること／限界 |
|---|---|---|
| [Solomon SSD1677 datasheet](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1267/SSD1677.pdf) | Rev 1.0, Nov 2018。印刷P15 §6.5 RAM、P16 §6.6 TP/RP/VS、P17 §6.7 LUT、P27/P28 0x21/0x22 | 4 RAM組合せ、位相期間・繰返し、112-byte LUT配置、電圧レジスタ、Mode 2。対象パネル向けの候補波形を認証する資料ではない |
| [DKE EPD Module User Manual](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1267/EPD_Module_User_Manual.pdf) | DEPG0397BBS770F3。P4概要／OTP・MCU LUT、P6図面A0/2021-09-29、SSD1677表記 | 480×800、双安定表示、ホストLUTが可能。候補ごとのDCバランス・寿命条件は得られていない |
| [M5Stack PaperMono公式資料](https://docs.m5stack.com/en/core/PaperMono) | 現行ページのE-Paper Usage Precautions、資料リンク | 約10回の高速／部分更新後のfull推奨。外部LUTのDCバランス、連続部分更新への注意。候補の長期適合性の証明はない |
| [M5GFX SSD1677実装](https://github.com/m5stack/M5GFX/blob/c5a3fefad0b38a52cc750e2c4761e339a69a7208/src/lgfx/v1/panel/Panel_SSD1677.inl) | commit `c5a3fefad0b38a52cc750e2c4761e339a69a7208`。LUT定義／_send_transition_planes | fastestでRAMを遷移クラスへ割り当てる実装。コメントの「near field-free」を電界ゼロ／DC安全性の保証と扱わない |

取得SSD1677 PDF SHA256:
`daf8b726f822f6907529386b8af6fbea6ec5e8ecc7b7af361340be911d632544`。
パネルPDF SHA256:
`6b65f56d5d549a132e5c807eef329fb3e997feab04ac06e8a107f01591618130`。
SSD1677 PDFの物理ページ番号と印刷ページ番号にはずれがある。上表は印刷番号。

ユーザー参照Writer：`E:\ESP32\M5Paper_Writerdeck`、HEAD
`86877a881a89b4867c561899f42bdfecd973af1b`。
実際に参照したインストール済みM5GFX `Panel_SSD1677.cpp` SHA256:
`c87a4c6a95a24d50d4d2750fe99215c655d673b040337523e3ace13fba093f58`。
別Device向けWriter処理をPaperMonoの根拠として流用していない。

## 4. 薄れの推定原因

原因は未確定。ユーザー報告から、fastestで更新履歴が増えると薄れ、
意図的なpartial比較では濃度が改善することが分かっている。
これは波形／保持中の画素への作用を調べる根拠になるが、特定原因を証明しない。

1. 新しい黒：白→黒の目的色パルスが短く、黒化量が不足する可能性。
2. 保持中の黒：同色のLUTはVSSのままで追加の黒化パルスがない。
   周囲の更新、VCOM、境界、履歴に伴う濃度変化を補う設計ではない可能性。
3. 同位置での消去／再描画：逆極性の準備パルスと目的色パルスの比率、
   正負の実電圧、温度等の影響があり得る。

old/new取り違え、未完了フレームのshadow化というソフトウェア要因は、
実ドライバのSPI捕捉・snapshot変更注入・失敗注入テストでは再現しない。
これで実パネルの光学状態とshadowの一致まで証明したわけではない。
40MHzはSPI通信速度であり、黒化用のパルス周波数と同じ数値ではない。
今回はSPIを下げて波形の問題を隠す変更をしていない。

## 5. baseline波形とpartialの違い

実装対象は `epd_ssd1677.c` とそこからincludeするreference backend。
呼出し元は既存display service／`solar_os_display_async.c`、snapshotを入力とする。
XORで差分がなければ転送なし。変更時は0x24へnew、0x26へcompleted oldを
それぞれ48000 bytes転送する。dirty boundsは診断値であり、矩形だけの物理駆動ではない。

| 項目 | baseline fastest | PaperMono partial |
|---|---|---|
| 波形 | ホストLUT：0x32の105 bytes＋0x03/04/2c電圧 | OTP、0x22=ff。OTPの内部波形内容は未取得 |
| 位相 | 非ゼロTPは6位相、計10 waveform frames。更新回数とは別 | 位相数・TP・遷移別パルスは不明 |
| 白→黒 | VSL 2 frames、VSH1 8 frames | OTPに依存、ホスト側から内訳不明 |
| 黒→白 | VSH1 2 frames、VSL 8 frames | 同上 |
| 黒→黒／白→白 | LUT0/3は全VS=0、source VSS。VCOMはDC設定 | 同上 |
| old/new | 毎回両RAMを全面上書き。成功後だけhost shadowを更新 | new 0x24の全面転送。既存OTPデモのRAM保持手順。partial後のhost 0x26書込みなし |
| 更新範囲 | native 800×480、2 planes=96000 bytes | native全面、new plane=48000 bytes |
| lifecycle | coldは0x22=cc、warmは0c。アナログON継続 | hardware resetで復帰、実行後0x10=01、既存100ms待機 |
| BUSY／全体時間 | warm driver約110ms、BUSY約52ms（詳細は§7/15） | 今回の比較対象外、同条件の新規実測なし |

RAMクラスは `(old,new)` の00=保持黒、01=消去、10=黒化、11=保持白。
モノクロに絞ると、参照M5GFXのBW=new_msb、RED=old階調の処理は
SolarOSのBW=new／RED=oldと一致する。双方の全RAM上書きにより、
転送範囲外の未更新遷移クラスを露出しない構成である。

baseline TPは `[2,2,2,2,0] [1,1,0,0,0]`、以降0。
VSは消去 `6a,a0`、黒化 `95,50`、保持黒／白／VCOMは0。
FRは全て8f、電圧bytesは `17,46,a8,32,30`。
baselineの110 bytesは上記pinした公式M5GFXのfastest LUTと完全一致する。
これらの**符号化**はデータシートと一致する。
source VSSとDC VCOMの差、実電圧、粒子移動、DC累積は別問題であり、
VS=0を「パネルに完全に電界がない」と断定しない。
full/cleanupは従来のOTP2 activation手順をそのまま保持する。

## 6. 比較候補と隔離

候補は2案。**両案とも実験用。DCバランス・寿命は未確認で正式採用できない。**

| 選択 | LUT差分 | 切り分ける仮説 | リスク |
|---|---|---|---|
| baseline | 差分なし | 現状比較 | 従来の薄れ |
| dose | byte50:2→1、byte51:2→3 | group0内で変化画素の逆極性準備を2→1、目的色を8→9 framesへ配分変更。黒化／白化の両側を変更 | net drive変化、消去残り、履歴依存、DC適合性未確認 |
| hold | byte0:0→90 | 保持黒にVSL 2→VSH1 2→VSS 6 frames。周囲更新中の既存黒を対象 | 新しい同色パルスでちらつき・薄れ・DC累積が悪化し得る |

両案とも10 frames、FR／電圧bytes、VCOM、温度選択、SPI40MHz、
tile、XOR、32行DMA、async、snapshot、shadow昇格、BUSY guard／timeoutは維持。
同じフレーム数でも実BUSY時間が同じと保証しない。
doseはgroup0の計8 frames／group1の計2 framesも保持する。
群をまたいで期間を移す変更を避け、FRの解釈に依存する時間変化を抑える設計である。
holdの正負2 framesは同じ長さでも、電圧絶対値やVCOM offsetが異なるため
DCバランスの証明にならない。

ビルド環境変数 `SOLAR_OS_SSD1677_WAVEFORM_TRIAL=1` の場合だけ候補を含める。
省略／0の通常版は候補LUTをリンクせず、候補用コマンドも拒否する。
通常版のELF／firmwareに候補LUT symbol／候補コマンド文字列がないことを確認。
比較版ELFにはdose/hold各110 bytesのLUT symbolがある。
最終比較版のcompile_commandsにtrial defineを確認し、ELFから抽出した
実際のLUTもdoseのbyte50/51=1/3、holdのbyte0=90であることを照合した。

比較版のみ:

```text
display mode display0 refresh=fastest
display mode display0 fastest-waveform=baseline
display mode display0 fastest-waveform=dose
display mode display0 fastest-waveform=hold
```

1行ずつ選ぶ。選択はRAMのみ、bootではbaseline。startupは変更しない。
MODE操作境界で直列化され、次の変更フレームでLUTを再ロードする。
選択だけでは同一画像に不要な更新を発生させない。
既存typingは、選択フィールドにかかわらず元のbaseline LUTを使用する。
partial／full／cleanupは候補LUTを使用しない。
新しいrefreshモードや通常操作へのpacing／自動fullは追加していない。

## 7. 速度計測

今回取得した新しいbaselineと候補を比較した。過去フェーズの速度は合格判定に使わない。
driverはv2 cycleの同一画像・範囲・回数、Writerはz4・単発5回・同じSPI40MHz/tile。

| 指標 | baseline | dose | hold |
|---|---|---|---|
| cycle warm n、driver p50/p95/max | 9 / 110.301 / 110.377 / 110.377ms | 9 / 110.290 / 110.482 / 110.482ms | cycle未測定。保持黒の目視で除外 |
| cycle warm BUSY p50/p95/max | 51.723 / 51.916 / 51.916ms | 51.820 / 51.899 / 51.899ms | cycle未測定 |
| 保持診断 warm BUSY p50/p95/max | 51.817 / 51.872 / 51.872ms | 51.825 / 51.903 / 51.903ms | 51.807 / 51.883 / 51.883ms（v2） |
| baseline矩形 convert／SPI p50 | 34.283 / 19.811ms（2 planes合計） | 各REFに記録。異なるboundsを混合集計しない | 同左 |
| Writer n、入力→完了 p50/p95/max | 5 / 163.475 / 171.482 / 171.482ms | 5 / 168.511 / 179.858 / 179.858ms | 未測定 |
| Writer p50+10%／p95+15%以内 | 比較基準 | 短い測定では目標内（+3.08% / +4.88%） | 未検証 |

各p95はn<20でmaxと一致する探索値。長い連続入力の遅延保証ではない。
Writerは新規テスト文書名が異なり、初回の見出しdirty boundsも異なる。
driverの異なるboundsを混ぜた速度受入れには用いない。
cycleのdriver平均は従来110.098ms／dose110.091ms、BUSY平均51.551ms／51.550ms。
cycleのconvert合計p50/p95は従来34.279/34.368ms／dose34.221/34.310ms、
SPI呼出し合計p50/p95は従来19.795/19.848ms／dose19.789/19.928ms。
保持診断のdriver平均は従来110.587ms／dose110.587ms／hold(v2)110.560ms。

`timing=on`のREFには開始／完了／busy、dirty bounds、96000 bytes、
diff／promote、2回のconvert／SPI、実SPI kHz、選択waveformを出す。
ログをwaveform・pattern・generation epochごとに分ける:

```text
python scripts/analyze_fastest_quality.py baseline-hold.log --json baseline-hold.json
```

warm成功・完全な記録だけを、waveform／SPI／path／dirty bounds別に集計。
重複ESP/CDCは除去。cold、失敗、不完全／破損記録は別表示し、受入れ資料としては拒否。
nearest-rank p95、p50=median、平均、max、nを報告する。n<20のp95は探索値。
1回のcold後にwarmを連続100回走らせる必要はなく、手動cleanupを挟む短い
同条件バッチを繰り返し、coldは別集計する。

Writerでは既存debug記録から入力dispatch、layoutを含むrender、submit、
snapshot copy、worker、置換chainを追った入力→完了を集計する。
layoutだけを独立計時する新しいWriter変更は加えていない。
SPI時間はpolling呼出し内の経過時間で、測定器による線上転送時間ではない。
BUSY完了も光学的な濃度判定ではない。

## 8. 実機品質比較の手順と結果

Writerの保存完了後にShellへ戻し、専用の一時ファイルへHILスクリプトを転送する。
既存ファイルを置換しない。終了後、その一時ファイルだけを削除する。
USBのPythonから実在を確認した `display0` をclaimするため、ZMKは不要。
対象が使用中ならエラーで中止し、文書・画像を強制終了／保存しない。

比較版でbaselineを選び、同条件でdose、holdを比較する。まず1回で異常確認、
その後最大10回の短いバッチ。候補はユーザー観察下だけで実行する。
同じ照明・温度・初期画像を用いる。各runの前後に手動cleanupを行い、
cleanupの正常性と元の黒濃度への復帰を確認する。

```text
display mode display0 timing=on
display mode display0 refresh=fastest
display mode display0 fastest-waveform=baseline
display mode display0 refresh=cleanup
python /quality-trial.py hold 10
```

`hold`: 左右の固定文字・細線・太線・checkerboardを保持し、小さいmarkerだけ更新。
`cycle`: 右の文字・線を同じ位置で黒→白→黒。
`add`: 右下の異なる位置へ黒文字を追加。
`erase`: 右の黒文字／線／checkerboardを白い帯で順次消去。
初期画像は小さい黒文字／太い文字／細線／太線／黒白交互領域を含む。
更新時に左領域を描き直さないため、保持黒の履歴を分離できる。

最初に白画面を1回presentし、直前に指定したcleanupを消費する。
このsetupの要求から白画面完了までにOTP fullが正常完了したことをログで確認する。
確認できなければ初期条件不一致として中止し、候補の判定に用いない。
白画面でSpace待ち、初回の診断画像でもSpace待ち、最後でq待ち。
引数10は白setupを除いた診断present数。ordinalとuptime_msをログへ記録する。
画面のカウンターは描かず、比較領域を汚さない。要求回数とREF完了回数は照合する。
診断内の500ms間隔は要求を分離する待ち時間で、通常描画の変更でも性能値でもない。

M5Stackの注意に合わせ、無人100回連続更新は行わない。
**30/100回のcleanupなし履歴評価は未実施**。これらは短いバッチ間のcleanupを
挟んだ評価と同一ではなく、置き換えて合格にしない。
長い履歴評価・正式採用には候補のDC適合性について追加根拠が必要。
評価が良くてもその根拠が得られなければ正式採用はBLOCKEDのまま。

| 評価 | baseline | dose | hold |
|---|---|---|---|
| 初回黒／保持黒／10回後 | 左右同程度／薄まりの印象はあるが目立つ変化なし | 初回やや濃い／10回後は右がやや濃い | 固定文字・線が薄くなった。保持黒案は不採用 |
| cleanupなし30/100回後 | 未検証 | 未検証 | 未検証 |
| 黒→白の消去残り（cycle 10回後） | わずかにあるがほとんど見えない | やや少ない気がする、左固定文字は従来と同等 | 未検証、保持黒で除外 |
| Writer単発入力の読みやすさ・ちらつき | 5回、z4、異常なし | 5回、問題なし | 未検証 |
| cleanup後の光学正常性 | 白setup後の初期表示に異常なし | 初期表示に汚れ・残像・ちらつきなし。最終cleanupの目視は別記 | 長い履歴後の光学評価は未検証 |

## 9. 自動テスト結果

- Host全体：**132 executables PASS**。従来131に比較ビルドの実ドライバ検証を追加。
- SSD1677候補版：1ms tickに加え10ms tickでも同じfixtureを実行しPASS。
- Python全体：**577 tests、573 PASS、4 skips、失敗なし**、16.581秒。
- 最終比較版ビルドPASS：154.58秒。通常版ビルドPASS：152.82秒。
- `git diff --check` PASS。
- 診断画面の説明修正後、HILの既存4テストを再実行してPASS。

Hostは実際のdriver.cを含め、全候補のRAM遷移4クラス、10 frames、
FR／電圧不変、warm／同一画像skip、SPI40MHz、immutable source、shadow昇格、
SPI／LUT／BUSY失敗とfault latch、cleanup／partial／typing非干渉を検証。
通常版では候補コマンド拒否を確認。
既存async snapshot所有権・置換、Writer、Sketch canvas／lifecycle、
BLE、Touch、RTC、Battery、I2CのHost回帰もPASS。

追加Python検証はログ重複、p95、異なる条件の分離、破損／未完了／失敗の排除、
async置換完了の対応、HILの回数・早期中止・display解放を検証。
ソフトウェアで波形の構成を確認できても、光学濃度／パネル寿命は確認できない。

証跡：`.research/fastest-quality/host-final.log`、`python-final.log`、
`build-trial-final.log`、`build-normal.log`。SDK regressionの一部は未修正版の失敗を
意図的に再現するためstderrにassertionが出るが、修正版確認とテスト全体のexit=0を照合済み。

## 10. Writer／Sketchの回帰

今回の自動回帰は上記のとおりPASS。実機Writerは従来とdoseの単発各5回を確認。
前フェーズのPASSを今回の候補のPASSとして転記しない。

5文字／20文字・編集・KEY1復帰、候補SketchのPen・PNG・Touch leaseの実機回帰は
今回未実施。候補を正式採用しないため、未確認項目をPASSへ置き換えない。
通常版への復帰時に保存データ・設定・bondを照合し、保持を確認した。
Sketchのラグ／UI文字サイズは今回の変更・合否対象に加えない。
候補除外版へ復帰後のUSB検査は正常。最後にZMKからShellの`version`を実行し、
`writer /quality-dose-speed-20261010.md`で`aaaaa`・見出しz4・表示を確認して
EscでShellへ戻る手順を依頼。ユーザーから「以前通り」と回答を得た。
この通常版の手動入力・再読込・表示確認はPASS。
hold／doseでのShell・Writerへの追加手動入力は別の検証候補であり、
本報告の結果には含めない。通常版へ候補波形を再適用してはいない。
ユーザーのpartialへの意図的な切替も不具合扱いしない。

## 11. 採否とStageの状態

| Stage | 状態 |
|---|---|
| 1 分析・計測 | 完了。一次資料・参照Writer・従来のdriver／Writerを確認 |
| 2 候補 | 2案を隔離実装、構成・回帰・ビルドPASS。短期速度は従来に近い |
| 3 目視A/B | 初回／10回・消去・単発入力を実施。holdは薄まりで除外。30/100回は未検証 |
| 4 通常版採用 | BLOCKED。保持黒の改善未実証、DC／寿命の根拠なし。候補は正式採用しない |

両候補**正式不採用**、baseline維持。doseは消去品質に小さな改善の印象があるが、
本目的の保持黒の薄まりに効果を示していない。holdは実機で保持文字が薄くなった。
速度目標内という短期結果だけで採用しない。
次に価値がある調査は、対象パネルに適合したメーカー検証済みの保持黒波形・
実電圧と温度を含むDC条件の入手、およびOTP partialとホストLUTの遷移別駆動の比較。
第三の未検証パルスを追加して無人長期駆動するより、適合性の根拠を得てから再評価する。

## 12. Firmwareとrollback

| artifact | bytes | SHA256 |
|---|---:|---|
| `build/papermono-sketch/firmware.bin`（従来、rollback用） | 3346736 | `599d3f5023200869c4aa8dd9768b63977b248ac1ae3456d262a85d26302d6471` |
| `build/papermono-fastest-quality-trial/firmware.bin`（候補opt-in、書込み済み） | 3347264 | `637e08c066bbfdeab04fd67f4e452c0dc3952b4dd4d48ffc79e9056f5fd400e4` |
| `build/papermono-fastest-quality-normal/firmware.bin`（候補除外、最終書込み済み） | 3346720 | `e1f217076d842a1e29bb4b2b56afc390942df49edfb5199797e651f08de4b806` |

全てapp partitionの0x700000 bytes以内。app addressは0x10000。
3版のpartitions.bin SHA256は同一:
`fd976d7c1eaa8f9d0a01ac87a8d05fc1cb1e59d3c5970954b30ad0c3aa12a488`。
ビルドが生成したfactory結合binはflash対象にしない。
比較版・通常版ともCOM39の対象機器を再確認し、firmware.binだけを0x10000へ書き込んだ。
`erase_flash`、bootloader／partition／ota_data／NVS／FAT書込みは行わない。

同一比較版での即時rollback:

```text
display mode display0 fastest-waveform=baseline
display mode display0 refresh=cleanup
display mode display0 timing=off
```

恒久rollbackは従来Sketch firmware.binをapplication-onlyで戻すか、通常版へ戻す。
現行設定がfastestなので、startupに候補オプションを追記しない。
通常版は試験selector／候補LUTを含まない。実機へ追加したHIL一時ファイル2本も削除済み。
実験用コードは専用defineとこのブランチに隔離し、診断の再現性を保持する。

## 13. データ保護

COM39のMACと実機partition tableを照合し、アプリ領域0x10000だけを書き込んだ。
NVS・FAT・otadataをバックアップし、書込み直後・アプリ起動前に全バイト一致を確認。
証跡: `.research/fastest-quality/flash-20261010-173910/summary.json`。
起動後にstartupの2行、Shell、RTC／Battery応答、ZMK自動再接続を確認した。
Writer入力測定は新規 `/quality-speed-20261010.md` と
`/quality-dose-speed-20261010.md` を使用し、既存文書を編集しない。
ローカルの従来artifactを保存し、新版は別ディレクトリへ保存した。

dose測定後、自動送信したCtrl+sがlegacy Writerでは文字sになったため、
その1文字をBackspaceで削除し、ZMKの実Ctrl+Sで保存した。
保存内容は5 bytesの`aaaaa`、saved checkpointはchecksum正常・z4。
この診断操作は単発5回の計測後で、性能集計に含めない。
baseline用の新規テスト文書は通常ファイルへ保存されていないが、新規recoveryを保持した。

最終の通常版書込みでもNVS／FAT／otadataは前後全バイト一致。
証跡: `.research/fastest-quality/flash-20261010-183520/summary.json`。
実験開始前と最終書込み前の論理データ比較もPASS:

- 既存44ファイルはShell履歴以外の内容が同一。既存文書・PNG・startup・設定を保持。
- 追加は新規テスト文書1本とテスト用recovery 2本だけ。HIL一時ファイルは残していない。
- NVS 38キーのうち、変化はWireGuardの単調増加するboot予約だけ。
  5 bondキー・remembered peer・Touch/Battery/RTC関連設定は同一。
- 候補除外版のboot後、startupの2行とrefresh=fastest、Shell、
  BLE接続済み／remembered 1/1、Battery・RTC応答、保存文書`aaaaa`を確認。

証跡: `.research/fastest-quality/logical-audit-final.log`、
`normal-restore-preparation.log`、`normal-postflash-verification.log`。
バックアップのbond実値は報告書・gitに含めない。

## 14. 残る制約と受入れ判定

最大の制約は長い更新履歴での保持黒改善と長期DC適合性が未確認であること。
構成の合法性、電圧bytes不変、10 frames維持だけでは寿命を保証しない。
OTPの内部波形も未取得のため、partialの改善を特定パルスへ帰属できない。
無人の長い候補駆動や自動fullによる問題隠しは行っていない。
比較実装と短期実機検証の報告であり、通常版の薄まりを解消したという報告ではない。

| 項目 | 判定 |
|---|---|
| Writerの初回表示濃度 | PASS（短期の単発入力は両方式で問題なし）。dose診断初回はやや濃い印象 |
| Writerの連続更新時の濃度 | 未検証（長い入力履歴）。holdは固定文字の薄まりでFAIL、doseの改善未実証 |
| 黒→白の消去品質 | PASS（短期表示に支障なし）。doseは残像がやや少ない印象、定量的な改善保証なし |
| fastest級速度維持 | PASS（短期の指定目標内）。一般的な速度受入れは未検証 |
| p95遅延悪化なし | PASS（指定+15%以内、dose +4.88%）。n=5の探索値であり遅延ゼロとはしない |
| cleanup互換性 | PASS（ソフトウェア・実機OTP完了）。長い履歴後の候補の光学評価は未検証 |
| Sketch描画品質 | 未検証 |
| Sketch機能回帰 | PASS（自動）／未検証（候補の実機） |
| SSD1677安全性 | BLOCKED（候補のDCバランス・寿命根拠待ち） |
| BLE/Touch/I2C回帰 | PASS（自動・BLE自動接続・RTC/Battery応答）。今回の実機Touch反復は未検証 |
| ユーザーデータ保持 | PASS（両書込みのNVS・FAT・otadata全バイト一致、既存44ファイル・bondの論理照合） |

今回の結論は従来fastest維持。Sketch UI再設計や独自描画高速化には着手していない。

## 15. 帰席後の実機baseline（2026-10-10）

比較版でselector=baseline、SPI actual=40MHz、tile経路を使用。
白画像のOTP cleanup正常完了後、矩形の左右交互更新を測定。
初回coldは450.799msとして除外し、同じdirty boundsのwarm 10回を集計した。

| 区間 | p50 | p95 | max |
|---|---:|---:|---:|
| driver全体 | 109.982ms | 110.699ms | 110.699ms |
| BUSY | 51.385ms | 51.963ms | 51.963ms |
| 2 plane変換合計 | 34.283ms | 34.365ms | 34.365ms |
| 2 plane SPI呼出し合計 | 19.811ms | 19.818ms | 19.818ms |

n=10のp95は探索値。各更新96000 bytes、err=0、不完全な計測なし。
ユーザーは矩形の濃さ・白地の汚れ・残像・欠けについて「異常なし」と確認した。
証跡: `.research/fastest-quality/baseline-box-40mhz.log` と同名 `.json`。

Writerは新規テスト文書、z4、同じ40MHz／tile／baseline。
ZMKでaを1文字→表示後5秒待つ操作を5回。5入力・5描画・5submit・5完了を照合し、
欠落・置換・エラー・不完全な計測なし。ユーザーはz4・表示異常なしを確認した。

| 区間 | p50 | p95 | max |
|---|---:|---:|---:|
| 入力→表示完了 | 163.475ms | 171.482ms | 171.482ms |
| 入力dispatch | 5.376ms | 8.368ms | 8.368ms |
| Writer render（layout含む） | 2.288ms | 2.344ms | 2.344ms |
| submit | 1.433ms | 1.483ms | 1.483ms |
| snapshot copy | 1.229ms | 1.273ms | 1.273ms |
| worker | 116.426ms | 118.120ms | 118.120ms |

これもn=5の探索値。driverはdirty bounds別に各n=1なので、混合集計で候補のp95判定をしない。
証跡: `.research/fastest-quality/writer-baseline-input.log` と同名 `.json`。
候補の短期結果は以下。長期安全性は未確認。

保持黒の診断（pattern=hold）も従来波形で開始した。ユーザーは初回の左右について
「濃さは両方とも同じ程度」と確認。その後、小さいmarkerだけを9回更新し、
診断要求10回＝REF完了10回（cold 1、warm 9）を照合した。
warmのdirty boundsは全て424,416,16,16、driver p50=110.613ms、p95/max=110.645ms。
エラー／不完全な計測なし。cleanup前の10回後、ユーザーは
「やや薄くなった気はするが目立った変化はない。汚れやちらつきもない」と確認した。
残像の程度は別途定量化していない。これを薄れ完全解消とは判定しない。
診断終了後のShell復帰に伴う別boundsの1更新は診断10回に含めない。
証跡: `.research/fastest-quality/baseline-hold.log` と同名 `.json`。

次にdoseの同じhold診断を実施。白setupのOTP full（err=0、4519.056ms）を確認後、
初回画像を1回表示。ユーザーは「やや濃い。汚れや残像ちらつきはない」と確認した。
REFはerr=0、cold=1、actual SPI=40MHz、tile、waveform=dose、451.672ms。
これは初期化を含むcold値であり、warm性能や品質改善の判定には使わない。
その後markerだけを9回更新し、診断要求10回＝REF完了10回を照合。
warm 9回のactual SPI／path／dirty boundsは従来波形のhold診断と同一。

| hold診断のwarm driver時間（各n=9） | baseline | dose | dose / baseline |
|---|---:|---:|---:|
| p50 | 110.613ms | 110.609ms | 1.0000 |
| p95 / max | 110.645ms | 110.651ms | 1.0001 |

doseのBUSY p50=51.825ms、p95/max=51.903ms。エラー／不完全な計測なし。
短い同条件診断で速度低下は見られないが、n=9のp95は探索値であり、Writer入力速度や
長期安全性の受入れとは区別する。cleanup前のdose 10回後、ユーザーは
「右(changed)の方がやや濃い。見た目の異常はない」と確認した。
このholdパターンでは左右の文字をどちらも描き直していない。
画面のRIGHT: changed表記はcycle／erase／addパターン用であり、
ここでの左右差を「描き直す黒と保持黒の差」とは解釈しない。
初回からの絶対的な濃度変化も、この回答だけでは確定しない。
証跡: `.research/fastest-quality/dose-hold.log` と同名 `.json`。

続いて候補holdの同じhold診断を開始。白setupのOTP full err=0を確認し、
初回画像を表示。REF err=0、cold=1、actual SPI=40MHz、tile、
waveform=hold、total=450.704ms、BUSY=134.381ms。
ユーザーから「何を比較しているか、dose／holdの意味が不明」と指摘されたため、
このrunは初回だけで中止。目視評価は未取得で、候補の良否の根拠に使わない。
証跡: `.research/fastest-quality/hold-hold.log`。

説明を改善し、3種類の波形は画面全体へ順番に適用していること、左右が別候補では
ないことを明記した。doseは書き換える画素の駆動配分を変更する案、holdは保持黒へ
駆動を追加する案。いずれも試験名で、名前自体は効果の実証を意味しない。
HILのholdパターンは画面表記を「FIXED TEXT A／FIXED TEXT B」、
「Only the small square changes」へ変更。他パターンでは右をedit areaと表記する。
表示内容の変更だけで、更新の間隔・回数・driver・候補LUTには変更なし。
HILの既存4テストPASS。新しい一時ファイルv2で、直前に見た文字の濃さが
marker更新後に変わるかを確認する。以前の画面の濃さの記憶には依存しない。

holdをv2で再試行。ユーザーが更新前の左右を確認してからmarkerだけを9回更新。
診断要求10回＝REF完了10回（cold 1、warm 9）、全てerr=0。
warmはactual SPI=40MHz、tile、dirty bounds=424,416,16,16、
driver p50=110.587ms、p95/max=110.647ms、BUSY p50=51.807ms、p95/max=51.883ms。
ユーザーは「薄くなっている。小さい印以外は薄い。小さい印は濃く見やすい」と確認。
保持黒の改善効果が得られず、holdは採用候補から除外する。
速度は探索値として従来に近いが、
v2は説明文を変更しており、v1との完全に同一の初期framebuffer比較ではない。
候補の速度受入れを確定する際は、v2の同じ画面で各候補を再計測する。
証跡: `.research/fastest-quality/hold-fixed-v2.log` と同名 `.json`。

holdを除外した後は従来波形へ戻し、OTP full cleanupで初期状態をそろえてから
v2のcycleパターンを開始。左の文字を固定し、右の文字／線を黒→白→黒と切り替え、
10回目は右が白の状態で停止。REF完了10回（cold 1、warm 9）、err=0、
40MHz／tile。保持黒の薄まりと消去残りを別々に確認した。
ユーザーは右の白地について「わずかにあるがほとんど見えない」、左の固定文字について
「かなり濃いとは言えないが読みやすい黒」と確認した。
warm driver p50=110.301ms、p95/max=110.377ms（各n=9、探索値）。
証跡: `.research/fastest-quality/baseline-cycle-v2.log` と同名 `.json`。

doseでも同じv2のcycle画像・白setup・回数・範囲で比較した。
診断要求10回＝REF完了10回（cold 1、warm 9）、err=0、不完全な計測なし。
warm driver p50=110.290ms、p95/max=110.482ms。従来cycleに対する比は
p50=0.9999、p95=1.0010。短い同条件診断で速度低下は見られない。
cleanup前の右の消え残りについて、ユーザーは「やや少ない気がする」と回答。
左の固定文字は「先ほどと同等の読みやすい黒」と確認した。
消去のわずかな改善の印象はあるが、保持黒の濃度改善を実証する結果ではない。
証跡: `.research/fastest-quality/dose-cycle-v2.log` と同名 `.json`。

doseの新規 `/quality-dose-speed-20261010.md` でz4・単発5回を測定。
5入力・5render・5submit・5worker・5物理完了を照合し、欠落・置換・エラーなし。
ユーザーは文字の濃さ・読みやすさ・ちらつきについて「問題なし」と確認した。

| Writer入力→表示完了（各n=5） | baseline | dose |
|---|---:|---:|
| 平均 | 159.355ms | 162.233ms |
| p50 | 163.475ms | 168.511ms |
| p95 / max | 171.482ms | 179.858ms |
| worker p50 | 116.426ms | 116.509ms |
| worker p95 / max | 118.120ms | 117.444ms |

入力完了の追加時間は中央値5.036ms（+3.08%）、p95 8.376ms（+4.88%）。
driverのcycle比較は中央値-0.011ms、p95+0.105ms。変動を超えた速度差とは断定しない。
doseのdispatch p50/p95は9.372/11.348ms、render 2.400/2.541ms、
submit 1.448/1.513ms、snapshot 1.242/1.289ms。
証跡: `.research/fastest-quality/writer-dose-input.log` と同名 `.json`。
測定後の保存操作・cleanupはこの5回の集計に含めない。
