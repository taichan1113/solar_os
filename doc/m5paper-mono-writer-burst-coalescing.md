# PaperMono Writer入力burst統合 trial — 実装・Host評価

2026-10-13。基点 `8113086a`、branch `experiment/papermono-writer-burst-coalescing`。
**実装・自動検証は完了。実機flash、C0/C1、今回の動画による光学評価は未実施。**
ユーザーは離席中の実装完了を指定し、録画は帰席後とした。production採用は行っていない。

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

## 帰席後の実機手順（未実施）

添付§12の「実機への書き込みと試験実行の前にはユーザーの明示的な許可を得る」に従う。
実装済み候補と上記Host tradeoffを提示してから、flash/各試験の許可を受ける。

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
**現在の光学C/R・実機速度・保護領域比較・Shell実機復帰はすべて未確認**。
旧動画は別カメラ位置なので今回のC0/C1として採用しない。

動画ツール：`capture_writer_quality.py --mode burst --waveform baseline --policy normal|coalesced`。
`--video-auto-following`は明示許可した当該1バッチのT0後だけ進め、final gateは手動。
`analyze_writer_quality.py`→`compare_writer_burst.py --basis hardware`、
`measure_writer_video.py`→`analyze_writer_contrast_timeline.py`を使う。
新camera pose ID/native cropのないburst timeline解析は拒否する。

## 現時点の判定

**CONDITIONAL**。Hostでは初回応答/最終一致を維持し、既存pending置換より1回減らせた。
追加遅延が許容され、実機でも更新数が減り、同条件の光学比較が改善傾向を示すなら、
Writer本番採用の候補に進める。ただしそれは本番適用の承認ではなく、実BLE入力、
カーソル/編集/保存の追加回帰が必要。

現時点では候補1設定を保留し、実機A/Bを先に行う。品質改善も許容応答性も未確認なので
本番採用やSketchへ移る判断は行わない。更新数削減が実機で消える、または254ms程度の
最大遅延が使用感を損なうなら再設計/見送り。無制限に追加波形・パラメータ試験は行わない。
