# PaperMono fastest表示品質 — 波形比較の準備報告

2026-10-10。**Stage 2のソフトウェア準備まで。改善の実証・正式採用は未完了。**
ユーザーが離席中のため、今回はCOM39へのアクセス、flash、パネル駆動、
同条件の実機計測、目視評価を行っていない。Stage 1の実測とStage 3以降は待機。
通常ビルドと起動設定は従来fastestを維持する。

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
| BUSY／全体時間 | 今回同条件の実測は未実施 | 今回同条件の実測は未実施 |

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

今回の新しい実機baselineは未取得。過去のdriver約114ms／入力完了約156msを
今回の測定結果・合格判定として使わない。

| 指標 | baseline | dose | hold |
|---|---|---|---|
| n、driver p50/p95/max | 0／未測定 | 0／未測定 | 0／未測定 |
| BUSY p50/p95/max | 未測定 | 未測定 | 未測定 |
| convert／SPI | 未測定 | 未測定 | 未測定 |
| 入力→完了 p50/p95/max | 未測定 | 未測定 | 未測定 |
| p50+10%／p95+15%以内 | 比較基準待ち | 未検証 | 未検証 |

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

## 8. 実機品質比較の手順と未実施結果

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

`hold`: 左の固定文字・細線・太線・checkerboardを保持し、周囲のmarkerだけ更新。
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
| 初回黒／保持黒／10回後 | 未検証 | 未検証 | 未検証 |
| cleanupなし30/100回後 | 未検証 | 未検証 | 未検証 |
| 黒→白、消去残り、境界、ちらつき | 未検証 | 未検証 | 未検証 |
| 体感速度・長い停止 | 未検証 | 未検証 | 未検証 |
| cleanup後の光学正常性 | 未検証 | 未検証 | 未検証 |

## 9. 自動テスト結果

- Host全体：**132 executables PASS**。従来131に比較ビルドの実ドライバ検証を追加。
- SSD1677候補版：1ms tickに加え10ms tickでも同じfixtureを実行しPASS。
- Python全体：**577 tests、573 PASS、4 skips、失敗なし**、16.581秒。
- 最終比較版ビルドPASS：154.58秒。通常版ビルドPASS：152.82秒。
- `git diff --check` PASS。

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

今回の自動回帰は上記のとおりPASS。今回の候補波形での実機回帰は未実施。
前フェーズのPASSを今回の候補のPASSとして転記しない。

実機では別の新規テスト文書を使って単発／5文字／20文字、改行・削除・
カーソル・スクロール、保存・再読込、KEY1復帰を確認する。
SketchはPenの薄れ、PNG保存・再読込、Touch lease解放を最小確認する。
BLE再接続、touch ON/OFF、RTC／batteryも短い回帰確認を行う。
Sketchのラグ／UI文字サイズは今回の変更・合否対象に加えない。
ユーザーのpartialへの意図的な切替も不具合扱いしない。

## 11. 採否とStageの状態

| Stage | 状態 |
|---|---|
| 1 分析・計測 | ソース／一次資料分析と再現画面準備済み。新規実機baseline待ち |
| 2 候補 | 2案を隔離実装、構成・回帰・ビルドPASS。実BUSY／速度による除外判断は待ち |
| 3 目視A/B | ユーザー帰席後。未検証 |
| 4 通常版採用 | BLOCKED。品質／速度／DC根拠が揃っていない |

現時点は両候補**正式不採用**、baseline維持。
改善量や追加時間が不明なため、どちらが優れるか推奨しない。
目視結果・同条件p50/p95・安全性根拠に基づいて採否を決める。
速度が悪化する案、ちらつく案、安全性不明の案を自動採用しない。

## 12. Firmwareとrollback

| artifact | bytes | SHA256 |
|---|---:|---|
| `build/papermono-sketch/firmware.bin`（従来、実機にある版） | 3346736 | `599d3f5023200869c4aa8dd9768b63977b248ac1ae3456d262a85d26302d6471` |
| `build/papermono-fastest-quality-trial/firmware.bin`（候補opt-in、未flash） | 3347264 | `637e08c066bbfdeab04fd67f4e452c0dc3952b4dd4d48ffc79e9056f5fd400e4` |
| `build/papermono-fastest-quality-normal/firmware.bin`（候補除外、未flash） | 3346720 | `e1f217076d842a1e29bb4b2b56afc390942df49edfb5199797e651f08de4b806` |

全てapp partitionの0x700000 bytes以内。app addressは0x10000。
3版のpartitions.bin SHA256は同一:
`fd976d7c1eaa8f9d0a01ac87a8d05fc1cb1e59d3c5970954b30ad0c3aa12a488`。
ビルドが生成したfactory結合binはflash対象にしない。
将来flashはCOM39の対象機器を再確認し、firmware.binだけを0x10000へ書き込む。
`erase_flash`、bootloader／partition／ota_data／NVS／FAT書込みは行わない。

同一比較版での即時rollback:

```text
display mode display0 fastest-waveform=baseline
display mode display0 refresh=cleanup
display mode display0 timing=off
```

恒久rollbackは従来Sketch firmware.binをapplication-onlyで戻すか、通常版へ戻す。
現行設定がfastestなので、startupに候補オプションを追記しない。
採用時には試験selector・HIL一時ファイルを整理し、選択LUTを別途レビューする。

## 13. データ保護

今回deviceへの通信／flash／ファイル操作はなし。
NVS、FAT、文書、PNG、`/.shell/startup`、bond、font、touch、battery、RTC設定は未変更。
そのため今回のdevice before/after raw比較は採取していない。
将来flash前後には従来の保存・設定／bond比較を実施する。
ローカルの従来artifactを保存し、新版は別ディレクトリへ保存した。

## 14. 残る制約と受入れ判定

最大の制約は実パネルの光学結果と長期DC適合性が未確認であること。
構成の合法性、電圧bytes不変、10 frames維持だけでは寿命を保証しない。
OTPの内部波形も未取得のため、partialの改善を特定パルスへ帰属できない。
無人の長い候補駆動や自動fullによる問題隠しは行っていない。
この実装は比較準備であり、通常版の画質を改善したという報告ではない。

| 項目 | 判定 |
|---|---|
| Writerの初回表示濃度 | 未検証 |
| Writerの連続更新時の濃度 | 未検証 |
| 黒→白の消去品質 | 未検証 |
| fastest級速度維持 | 未検証 |
| p95遅延悪化なし | 未検証 |
| cleanup互換性 | PASS（ソフトウェア）／未検証（候補の光学結果） |
| Sketch描画品質 | 未検証 |
| Sketch機能回帰 | PASS（自動）／未検証（候補の実機） |
| SSD1677安全性 | BLOCKED（候補のDCバランス・寿命根拠待ち） |
| BLE/Touch/I2C回帰 | PASS（自動）／未検証（今回の実機） |
| ユーザーデータ保持 | PASS（device操作なし） |

次はユーザー帰席後の比較と実測。Sketch UI再設計や独自描画高速化には着手しない。
