# PaperMono Writer入力burst統合 trial — 実装・実機A/B評価

2026-10-13。基点 `8113086a`、branch `experiment/papermono-writer-burst-coalescing`。
**実装・自動検証・実機flash・C0/C1各1回と元動画T0/T1比較を完了。判定はCONDITIONAL。**
更新数は6→5回、初回応答は維持。最大遅延は220→303msへ増え、明確な品質改善は確認できない。
production採用は行っていない。入力開始基準の時系列解析も完了し、同期の限界を後述の証跡に記録した。

## 現行経路と追加統合の場所

CHAR dispatcherは`writer_handle_char()`でgap bufferを即時編集し、変更ごとに
`document_version`を増やす。`writer_mark_changed()`がparse/layout/render pendingを立てる。
40ms Tickはparse/layoutを更新し、render pendingならWriterの実描画を行う。
renderはGFX submitの成否を確認してpendingを解除する。PaperMonoの通常カーソル点滅はOFF。
RAM品質診断は比較画像を固定するためカーソル自体を描画しない。

executorはsubmit時にbufferをコピーし、activeとlatest pendingの2スロットを所有する。
active処理中の追加submitはpendingを置き換える。workerが完了したgenerationを記録して
次のpendingを処理する。waitは最新submitted generationの完了を待つ。
fault時はshadowを確定せずpendingを破棄し、次の更新を自動再試行しない。
このexecutor、SSD1677、LUT、SPI、tileには今回変更を加えていない。

今回の統合は**RAM WriterのTickでrender/presentを許可するタイミングだけ**に置いた。
CHAR→buffer反映とparse/layoutの頻度は維持する。したがってparse/layout削減を効果として
数えてはいない。parse/layout・render・submit各時刻をjournalに残して実機で確認する。

## 選定

`tests/hil/model_writer_burst.py`は40ms Tick・112ms warm処理・active/latest pendingを
使う決定論モデル。後続9文字を60ms間隔で入力する。下表は**実機測定ではない**。

| render許可間隔（burst中） | render数 | 後続物理更新 | 初回応答ms | 最大遅延ms | 最終遅延ms |
|---|---:|---:|---:|---:|---:|
| normal | 9 | 5 | 152 | 204 | 120 |
| 120ms | 5 | 5 | 152 | 212 | 152 |
| **160ms** | **4** | **4** | **152** | **252** | **152** |
| 200ms | 4 | 4 | 152 | 292 | 192 |
| 240ms | 3 | 3 | 152 | 332 | 152 |

既存pending置換だけでも9→5になるため、追加の改善余地は小さい。
120msは物理更新を減らせず、200msは160msと同じ更新数で遅延が増す。
240msはさらに1回減るが最大遅延増が大きいため今回は選ばない。
busy/pending専用の新しいexecutor連携、全アプリのpacing、固定500ms方式、
入力でdeadlineをリセットし続けるdebounceも採用しない。

160ms/休止80msの1設定をtrialで評価する。

* 休止後の最初の入力は次の通常Tickで描画する。debounce待ちは付けない。
* 60ms間隔の継続入力中は、前回**成功したsubmit**から160msで描画を許可する。
  新しい入力はこの最大deadlineを延長しない。
* 最終入力から80msの休止を検出すると、160ms未到達でもtrailing描画を許可する。
  40ms Tickの量子化が加わる。100ms間隔の入力は休止後のleadingとして扱う。
* suppressed Tickはrender pendingを消さない。成功submit時だけ描画済みversionを進める。
* 時刻は64bit monotonic us。タイマー/別task/終了後callbackは追加していない。
  suspended/fault時にtrial Tickはsubmitしない。終了は既存app state破棄で完結する。

## 実装範囲とコマンド

`SOLAR_OS_SSD1677_WAVEFORM_TRIAL=1`のprivate RAM診断だけを対象とする。
productionのWriterイベント処理はpreprocess後、変更前と空白以外が同一。
通常版ELFには`solar_os_writer_quality_*`シンボルとburst診断文字列が存在しない。
trialでも通常のWriterに新しい統合を適用しない。

```text
display test display0 writer-quality baseline burst
display test display0 writer-quality coalesced burst
```

ここで`baseline`はnormal描画policy、`coalesced`は今回のpolicy。
**波形は両方ともbaseline LUT**。journalにはwaveformとpolicyを別々に記録する。
既存`baseline|dose slow|fast [optical]`、`baseline video <count> <interval>`は維持する。

共通初期手順はOTP白cleanup→固定参照→中央20個のaを1回で形成→T0。
続いて`bcdefghij`を60msの絶対CHAR deadlineで自動入力する。
render pendingや完了待ちでCHARを止めず、途中の目視gateも入れない。
送信deadlineが40ms以上遅れた場合は同条件とみなさず停止する。
最後のinputを含むsubmit/物理完了を待ってT1とし、T0/T1とも最終完了後2秒以上で観測gateに入る。
撮影位置の変更に合わせ、動画解析では新しいcamera pose ID、四隅、native cropを指定する。
前回の四隅/homography/cropを自動で流用しない。

入力29イベント、render/submit予約最大10、Driver入口で11回目をSPI前に拒否する。
予約はactive/pending/replacedも含めるため、上限到達で再描画を強行しない。
最大96 Tickのbounded journal、5秒のburst進行timeoutも設けた。
fault後はcleanupを含め再試行しない。正常終了/abortは既存drain→正規cleanup→
owner解放→Shellキャッシュ無効化を使用する。C0とC1を自動連続実行する機能はない。

RAM診断はcheckpoint/recovery/metadata/文書ファイルを開かない。
fixtureのstorage/checkpoint境界は呼ばれたらassertする。
Host編集fixture用のprivate入力planはShellから設定できず、実機では固定29文字を使う。

## journalと応答性

入力sent/accepted/mutated/request、document version/hash、Tick時刻と許可/抑制、
parse/layout・render・submit各時刻、generation、snapshot version/hash、busy/pending/replaced、
Driver開始/終了、BUSY、変換、SPI、observerを記録する。worker内ではtyping ROI hashと
固定/先頭/20文字群ROI監査を維持し、UART出力はburst後に行う。

各入力を、その入力を含む**最初の完了snapshot**へ対応付ける。
pendingが置換されても後続snapshotに含まれれば欠落扱いしない。
最終snapshotのhash/versionと最終bufferを照合する。
応答性の基準はCHAR sent→Driver完了であり、肉眼での見え始めや光学完了の校正値ではない。
先頭20文字の形成と、その後の9文字の応答分布は別に報告する。

実Writer fixtureの結果（executorは決定論境界モデル。**実機値ではない**）：

| 指標 | normal | coalesced |
|---|---:|---:|
| 入力イベント | 29 | 29 |
| render/present | 10 | 5 |
| 物理更新（形成含む） | 6 | 5 |
| 後続物理更新 | 5 | 4 |
| pending置換 | 4 | 0 |
| 初回形成sent→完了ms | 394 | 394 |
| 後続先頭sent→完了ms | 154 | 154 |
| 後続p50 / p95 ms（nearest-rank） | 154 / 207 | 174 / 254 |
| 後続最大遅延ms | 207 | 254 |
| 最終文字→完了ms | 126 | 154 |
| burst中最大無更新時間ms | 154 | 160 |
| 最終buffer/version/snapshot | 一致 | 一致 |

形成込み16.7%、後続20%の物理更新削減。最大遅延は47ms増、最終遅延は28ms増。
Hostでは擬似SPI/BUSYを使い、112msの境界busyをDriver外側に加える。
そのためjournalのDriver p50=1ms/observer=0usはmock値で、速度評価の実測として使わない。
実executorのスロット所有・置換・drain・faultは別の`display_async_test`で確認した。

## 自動検証と成果物

* Host全回帰137本PASS。実Writer event→gap buffer→parse/layout→raster→submit→
  captured SPIの両planeを検証。60/100ms、途中休止、Backspace/Enter、version/hash、
  final shadow、suspended Tick、submit予約上限、Driver入口11回目、fault/abort、
  Shell復帰を確認。旧slow/fast/optical/video fixtureも通過。
* Python全回帰633件中629 PASS、4 skip。新規burstテスト8件PASS。
  入力欠落/重複/順序、snapshot/version不一致、fault/不完全復帰、不正上限、
  generation membership、未知の光学値、T0/T1の同経過時間対応を確認。
  skipは既存の環境依存テスト。実機確認のskipをPASSへ置き換えていない。
* PlatformIO `esp32_s3_devkitc1_n16r8`、board override `m5paper_mono`、
  flavor `writerdeck`、layout `ota`。trial/production両方SUCCESS。
* ELFシンボル・binary診断文字列のtrial/production分離と、production preprocessの
  空白以外の一致を確認。`.vscode/`は既存のユーザー未追跡ファイルのまま。

成果物は`.research/writer-burst-coalescing/`。Host journal/JSON/候補モデル/
比較図は`host/`、ビルドログと両firmware/ELFは直下に保存。

| firmware | SHA256 |
|---|---|
| trial | `e5e2ad2604060528fb07982d94a5ce70e2add79d327ca2ba3f4963dcc6e0c0c3` |
| production | `46bcc0db4a1e515de64f1d30886c4b97e2ad068b2bd00e0e6f4c2adad7afb224` |

主な変更ファイル：`src/apps/solar_os_writer.c`、`solar_os_writer_quality.inc`、
`solar_os_writer_quality_trace.h`、新規`solar_os_writer_burst_trial.h`。
Host fixture/Makefile、journal/capture/video/timeline解析、新規model/比較Python、
`tests/test_writer_burst.py`を更新。Services/Driver/LUTは変更なし。

## 実機手順と領域保護

添付§12の「実機への書き込みと試験実行の前にはユーザーの明示的な許可を得る」に従う。
実装済み候補と上記Host tradeoffを提示し、flashとC0/C1各1回の許可を取得した。

1. 未保存編集を保存しShellへ。COM39のESP32-S3/MAC、現在application hash、
   partition table、CRCを含むOTA選択/current app領域を実機から照合する。
   以前のCOM番号やOTA状態だけを根拠に書かない。
2. NVS/FAT/otadata/PHY全領域をbefore保存。選択applicationのみへtrialを書き込む。
   application hashのreadbackと、保護4領域のbefore/after一致を確認する。
   combined image、bootloader、partition/otadata初期imageは書き込まない。
3. Shell表示/設定を確認し、新しいカメラ位置でAF無効・反転OFFの画角を確認する。
4. **C0を1回**。白・参照・T0・T1・cleanup後Shellを目視。元動画/journalを保存する。
5. C0終了/復帰を確認してから**C1を別途1回**。同一入力・LUT・SPI・tile・UI140/z4・
   camera pose/照明・観測経過時間を維持。自動で二条件を連続実行しない。
6. 新しい四隅から元解像度/実PTSで解析。T0/T1同完了後経過時間とinput開始基準の
   時系列を出す。複数文字snapshotごとに1つの新規文字ROIを選び、物理generationへ
   対応させる。9文字と物理更新列を単純zipしない。光学offsetが不明なら明記する。

主評価は中央20文字群のC=(W−B)/W、R=C_T1/C_T0。
先頭a、OTP参照、背景W、位置ずれ感度、残像/ちらつきも確認する。
この手順でC0/C1を各1回実施し、白・参照・T0・T1・cleanup後Shellをユーザーが確認した。
旧動画は別カメラ位置なので今回のC0/C1として採用しない。

### 2026-10-13 実機書き込み完了

ユーザーからapplication-only書き込みとC0/C1各1回の試験許可を取得。
COM39のESP32-S3/MAC `28:84:85:44:79:68`、partition table、現在application SHA256
`bba838c9cac85aa4a43f4ca0e8d7f25cdb55d09ba83531698afb155c4f4714bd`を実機で照合。
otadataのseq=1/state=VALID/CRC一致から選択先ota_0（0x10000）を確認した。
trialを書き込み、readback SHA256は上記trialの値と一致。
NVS/FAT/otadata/PHYの全領域とpartition tableのbefore/afterは一致（PASS）。
再起動後、USB Shellの`version`/`sessions`/`display mode display0`が応答し、
foregroundはShell、refresh=fastest。目視表示と新カメラ位置の画角も確認した。
証跡：`.research/writer-burst-coalescing/flash-20261013-211813/summary.json`。
このPASSは書き込み・領域保護の結果であり、C0/C1の表示品質・応答性は次章で区別する。

動画ツール：`capture_writer_quality.py --mode burst --waveform baseline --policy normal|coalesced`。
`--video-auto-following`は明示許可した当該1バッチのT0後だけ進め、final gateは手動。
`analyze_writer_quality.py`→`compare_writer_burst.py --basis hardware`、
`measure_writer_video.py`→`analyze_writer_contrast_timeline.py`を使う。
新camera pose ID/native cropのないburst timeline解析は拒否する。

## 実機C0/C1の結果

両条件ともbaseline LUT、40MHz、tile、UI140%、Writer z4。RAM専用診断で初期20個のaを
1更新で形成し、T0を確認してから`bcdefghij`を60ms間隔で途中停止せず入力した。
各条件を個別に録画・終了・cleanupし、C0終了後にC1を開始。追加実機試験は0回。
NVS/FAT/otadata/PHYのflash前後一致はPASS、試験の文書はstorage=none。

| 指標 | C0 normal | C1 coalesced |
|---|---:|---:|
| 入力文字数（形成20＋追加入力9） | 29 | 29 |
| render/present要求 | 10 | 5 |
| 物理更新（形成含む） | 6 | 5 |
| 後続物理更新 | 5 | 4 |
| pending置換 | 4 | 0 |
| 初期20文字の最初の入力→形成完了ms | 505.564 | 506.518 |
| 休止後の最初の1文字→完了ms | 164.197 | 162.859 |
| 後続9入力→完了p50 ms | 164.197 | 202.080 |
| 後続9入力→完了p95/最大ms | 220.496 | 302.608 |
| 最終入力→最終完了ms | 148.976 | 241.012 |
| burst中の最大無更新時間ms | 164.197 | 200.437 |
| 後続更新の平均start間隔ms | 116.691 | 186.550 |
| Driver p50 ms（形成含む） | 114.961 | 112.309 |
| observer p50 ms | 0.826 | 0.802 |
| 最終snapshot/version/hash | 一致 | 一致 |
| fault/overflow | 0/0 | 0/0 |
| cleanup/Shell復帰 | ログ・目視とも正常 | ログ・目視とも正常 |

これはCHAR送信→その文字を初めて含むsnapshotのDriver完了であり、光学的可視時刻の校正値ではない。
物理snapshotの累積文字数はC0=`20,21,23,25,27,29`、C1=`20,21,25,28,29`。
全入力の順序・最終version/hash・generation membershipをjournalで確認した。
置換4件は欠落ではなく、後続snapshotに包含されている。
render要求は50%減だが、従来executorが既にpendingを統合するため、実物理削減は1回（形成含め16.7%）。
後続の先頭a/20文字群は毎更新でold/new保持黒を維持（47/940画素）、固定参照は589画素を維持した。
これは論理画素の一致であり、濃さを保証する証拠とは区別する。

### 元動画による光学比較

新camera pose ID=`20261013-burst-4k-moved`、元解像度3840×2160、約29.97fps。
ユーザーはカメラ/本体固定・AF無効・反転OFFの録画準備を確認。AE/WB固定と反射率校正は未確認。
予備動画：`WIN_20261013_21_20_53_Pro.mp4`。

| 条件 | 元動画 | SHA256 |
|---|---|---|
| C0 | `WIN_20261013_21_23_41_Pro.mp4` | `9561468c9f7278722d3a3593320cf1e07c63c6e19a832e0471d5ef620f2da9ec` |
| C1 | `WIN_20261013_21_39_24_Pro.mp4` | `85db208100ce4ed5fc3ee799c3171d2f3246b897fe92e80a3474965e7c937f6b` |

原本は`C:/Users/takec/Pictures/Camera Roll/`にあり、読み取りのみ。
元動画の四隅は予備画面で先に決めた`(615,120),(3551,242),(3624,1984),(476,1909)`を使用。
固定ROIで位置合わせし、元解像度の双線形sampleを使用。測定画像を拡大縮小しない。
中央a群は`(304,202,200,22)`/940黒画素、先頭aは47黒画素。
**独立固定参照はOTPで形成したPOSITION行の数字部`(164,202,120,22)`/589黒画素**。
画面上の上段「OTP reference」文字列そのもののROIではない。この区別を明示して解釈する。

両動画で音の合図候補は検出されなかった。初回形成の90%到達を視覚markerとし、
UART/ファイルUTCはその区間を探す用途だけに使用。markerはC0=173.974523s、C1=305.268359s。
T0/T1はそれぞれ推定完了後0.5/1.0/1.5秒の実PTSを抽出し、3フレームの中央値を比較。
絶対的なcamera/光学遅延・clock driftは未校正。上記経過時間は対応付けモデルに基づき、
正確な電気的完了との一致を保証した値ではない。

| 指標 | C0 | C1 |
|---|---:|---:|
| group T0 C | 0.485854 | 0.431709 |
| group T1 C | 0.342075 | 0.305545 |
| group R=C_T1/C_T0 | 0.704070 | 0.707755 |
| 先頭a R | 0.695728 | 0.701675 |
| OTP固定参照 R | 0.887357 | 0.894970 |
| group背景W変化 | +1.487% | +1.327% |
| ±0.25論理画素の独立位置ずれ感度によるgroup R範囲 | 0.515–0.972 | 0.531–0.920 |
| ユーザーのT1目視 | a群が薄くなった・背景正常 | a群が薄くなった・背景正常 |

R差は+0.003685（+0.37ポイント）で、今回の測定変動・位置ずれ感度に対し小さい。
位置ずれ範囲は保守的な感度幅で、信頼区間ではない。初期C自体にも差があり、
各条件1試行・未校正のカメラから品質改善や因果効果を主張しない。
固定参照の低下は白地の空間noise proxyより小さく、そちらも定量的結論は保留する。

T0でのユーザー確認待ちにより、形成完了から後続入力開始まではC0=63.241秒、C1=115.861秒だった。
後続9入力は両条件とも60ms間隔で中断なしだが、入力前の保持時間は同一ではない。
T0/T1の撮影時点を各完了後の同経過時間へ合わせても、この保持履歴の差は消えない。
そのため、更新数だけを品質差の原因とする判断は行わない。

### 証跡と時系列

`.research/writer-burst-coalescing/`の`C0/`・`C1/`にjournal、状態、解析JSON、元動画metadata、
T0/T1元解像度frame、registrationを保存。`observations.json`はユーザーの目視結果。
`hardware-comparison.json`は入力→完了と更新数、`combined-results.json`は光学値との統合。
比較図：`hardware-quality-summary.png`（誤差棒は位置ずれ感度で、統計的信頼区間ではない）。
時系列解析結果は`timeline-even-crop/`へ保存した。

最初の時系列試行はcrop高さ213をYUV420 decoderが212へ丸め、raw frame寸法が一致せず失敗した。
失敗出力`timeline/`は採用しない。cropを幾何学的に外側の偶数境界へ拡張し、
両条件共通の元画素領域`(1156,832,1696,214)`で再解析した。原本・測定ROI・実機更新数は変更なし。
動く文字の同期ROIはC0で`0,2,4,6,8`、C1で`0,4,7,8`を使い、
9文字と物理更新列を単純zipしない。入力開始を0秒とする時系列を、完了後同経過時間の比較と分ける。

| 時系列検証 | C0 | C1 |
|---|---:|---:|
| 抽出フレーム / ROI有効フレーム | 2077 / 2042 | 3682 / 3621 |
| T0/T1既存測定6フレームとの最大C差 | 0 | 0 |
| 新規文字による独立同期 | 3イベントで成立 | 不成立（有効2イベント） |
| 同期残差最大 / 経験的包絡 | 20.919 / 37.587ms | 未測定 |
| 主条件で個別更新の直前・直後を比較できた件数 | 0 | 未測定 |

C1では後半2つの同期セルに複数の10%閾値通過があり、事前の最低3イベント条件を満たさなかった。
閾値を変更して成立扱いにはしない。C1の各更新への光学的対応、個別ΔC、更新なし区間の傾き、
薄まり開始時刻は未測定。初回形成markerによるT0/T1比較とは別の判定である。
C0も100msの系統遅延感度、50msのsettling、経験的包絡を合わせた約188msの除外幅に対して
更新間隔が短く、個別更新の比較窓は取れなかった。件数0は「薄まりなし」を意味しない。

C0の入力直前−1.5〜−0.5秒と最終推定完了後0.5〜1.5秒を各30フレームで比較すると、
group Cは0.472272→0.339044、R=0.717899だった。T0からの長い保持時間を避けても
この動画ではburst前後の低下が見えるが、位置ずれ感度・未校正のカメラ・単一試行という限界は残る。
C0の最後の推定完了から2秒以降の38.64秒では、groupの記述的傾きは+0.000286 C/秒、
端点中央値差は+0.002685だった。更新後に継続して低下する証拠とはならず、
相関した動画フレームから有意差・独立標本数・薄まりの電気的原因は求めない。

最終図は`timeline-final-summary.png`。両条件の全保持期間と入力付近を元PTSで描き、
横軸は初回形成markerに基づく推定入力開始であることを明記した。
自動生成の`01-timeline.png`は従来の7秒表示範囲と不成立時のmarker原点が今回の長い保持時間に
適合しないため、比較図として採用しない。`04-event-deltas.png`の空欄も未測定を示す。
同期不成立を埋めたり、追加実機更新で取り直したりはしていない。

## 最終判定と提案

**CONDITIONAL：160/80ms設定を本番採用せず、更新統合の再設計を提案する（最終提案2）。**
初回応答・全入力・最終表示を維持し、物理更新を1回減らせた。一方、最大遅延は82ms増え、
最終表示は92ms遅くなった。Host最大254msに対し実機303msで、モデルの応答性見積もりにも差がある。
今回の動画では品質改善が明確でない。実BLE入力での使用感・編集・保存の採用回帰は未実施であり、
自動入力の成功をそのPASSに置き換えない。

再設計時は、実機のparse/raster/submit時間とTick位相をモデルへ取り込み、初回・最大・最終遅延を
先に制約し、既存pending置換より追加削減できる候補だけをHostで選ぶ。
新しい実機試験を行う場合は目的と期待する判断改善を提示し、別途許可を得る。
今回のtrialは終了して通常Shellへ戻り、通常Writerの描画方針は従来のまま。
本番採用、LUT/Driver変更、追加パラメータ試験、Sketch改善には進んでいない。
