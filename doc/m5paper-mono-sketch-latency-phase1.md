# M5Paper MONO Sketch Phase 1：入力・描画遅延の計測準備

## 結論と対象

PaperMonoのPen入力をFT6336Gから論理Canvas、非同期Display、SSD1677完了まで追跡できるtrial専用journalと解析器を用意した。Hostで再現可能な失敗と、実機でしか測れない区間を分けた。**今回の判定はCONDITIONAL**。実機への書き込み・再起動・測定は行っていないため、実際の追従遅延、画面上の線の出現時刻、見た目の品質は未測定である。改善アルゴリズムも実装していない。

作業ブランチは `experiment/papermono-sketch-latency-measurement`。基点はWriter burst再設計の `808925bb`。ユーザー文書、NVS/FAT/otadata、waveform試験設定と既存データには触れていない。Writer、SSD1677 LUT/電圧、SPI設定、UIスケール、タスク優先度、通常版のqueue容量と描画方針も変更していない。

## A. FT6336GからSketchまで

`boards/manifests/m5paper_mono.toml` は `pointer_ft6336` を組み込み、`touch0` を `ft6336`、I²C `i2c0` / `0x38`、rotation 0、managed powerに結び付ける。GT911はこの機種の入力経路ではない。ドライバー `src/drivers/ft6336.c` はタッチ状態・座標の6バイトをI²Cで取得し、サービス `src/services/solar_os_ft6336.c` は約10ms周期でpollする。成功した読取から座標範囲・回転を処理し、押下状態と前回座標との差でPRESS/MOVE/RELEASEを作る。同じ座標のMOVEは生成しない。enqueue成功時だけサービスの前回状態を進める。失敗時には次pollでその時点の接触状態を再試行するため、間にあった軌跡の復元は保証できない。

共通pointer FIFOは32件。満杯時は `ESP_ERR_NO_MEM` でMOVEもRELEASEも拒否し、古いイベントを自動的には間引かない。`src/main.c` は最大8件ずつ読み取るが、**空になるまで繰り返す**ため「1ループで最大8件」ではない。mainからSketchのイベント処理へ渡り、PenのPRESS/MOVEは前回点からの線分を2bpp packed Canvasに反映し、RELEASEは描画状態を終了してrender要求を残す。描画中の論理CanvasはDisplay executorのpending frame置換から独立している。

Sketch Tickは40ms。`sketch_render()` はCanvas全域をXBMへ変換し、GFXへbitmapを転送してToolbar等のUIを描画した後、immutable snapshotを提出する。executorはactiveと最新pendingを保持し、中間pendingを置換できる。したがって「入力点がCanvasに届かない」と「途中の表示frameが省略される」は別の結果になる。Display workerの終了はDriver完了であり、光学的に線が見え始めた時刻ではない。

## B. 完全性とHost検証

入力側ではPRESS/MOVE/RELEASEの通常系列、FIFO満杯時のMOVEとRELEASEの書込失敗、FIFO排出後のRELEASE再試行を既存Input fixtureで検査した。FT6336の既存Host driver/lifecycle試験とSketch Canvas/lifecycle試験も利用する。届いたPen MOVEがCanvas線分に残ること、RELEASEがrenderを要求すること、snapshotの置換で論理Canvasを消さないことは既存のSketch/Display fixtureで確認する。

追加fixtureで、RELEASE後のsubmitが失敗した場合を再現した。Canvas末尾は残る一方、現行 `sketch_render()` はエラー後にも `render_pending` を消し、次のTickだけでは再提出されない。suspend/resume後の全再描画では提出される。これは「最終frame表示成功」にはできない現行挙動であり、今回の変更で無条件retryは入れなかった。解析器もsubmit失敗、Driver失敗、非物理的なworker完了、最終Canvas versionの未完了を失敗として出す。Host fixtureでの再現を実機発生率と混同しない。

## C. trial専用計測

`SOLAR_OS_BOARD=m5paper_mono` かつ `SOLAR_OS_SKETCH_LATENCY_TRIAL=1` のときだけ計測モジュールとShellの `sketchtrace` コマンドをビルドする。通常版では計測モジュールをリンクしない。共通InputイベントのABIは変えず、trial時だけFIFOに並走するpoint IDのsidecarを追加した。容量、ready判定、dispatch、描画方針は維持した。

`sketchtrace start` で最大4096行のPSRAM journalを開始し、`stop` で凍結する。`status` は行数・overflow・observer時間の合計/最大を返す。`dump <offset> [1..128]` は停止後に番号付きCSVを分割出力する。描画中は大量のUSBログを送らない。IDと単調時刻は、FT6336 sample時に始まり、FIFO投入・取出し、main dispatch、Sketch受信、Canvas version、render、submit generation、pending置換、worker、SSD1677まで残る。`SAMPLE.version` にstroke ID、`SAMPLE.point` に連番を置く。trialのFIFO sidecarはsource close/quiesce時の圧縮にも追従する。journalが溢れた場合、欠測を推測して補完しない。

記録対象は接触中・離上時・失敗時のI²C read、sample→投入、FIFO待ち、Sketch event、Canvas線分処理、Canvas→Tick、全域XBM変換、GFX bitmap転送、UI描画、submit、worker待ち、SSD1677総時間と既存timing値からの変換・SPI・activation/ready時間である。無接触の正常pollはjournal容量を浪費しないよう記録しない。SSD1677の変換/SPI内訳はtile経路の既存timing値を利用する。band bufferを確保できずscalar経路へフォールバックした場合は内訳が0となり、総時間だけが有効なので実機ログで経路を確認する。Driver完了時刻までを記録し、画面の光学応答は別途動画で測る。各recordのobserver時間を集計するため、実機ではその負荷も報告する。I²Cのpoll間に実際に指が動いた時刻はFT6336のsampleからは分からない。

解析器 `tests/hil/analyze_sketch_trace.py` は `SKTRACE_META` と全行のdumpを受け、時刻順、連番、overflow、FIFO失敗、提出世代の置換/完了、RELEASE最終frame、区間ごとのp50/p95/最大をJSONへ出す。`SKTRACE_META` と全行が揃わない場合は解析を拒否する。`WORKER_END` はエラーなし、かつ `physical=1` の場合だけ完了として数える。Hostの合成ログは解析ロジックのテストであり、遅延測定値ではない。

| 区間 | Hostでの確認 | 実機値 |
|---|---|---|
| T1 FT6336 I²C read | API成功/失敗の経路 | 未測定 |
| T2 sample→FIFO投入 | IDと失敗分類 | 未測定 |
| T3 FIFO→main dispatch | FIFO順序・drain | 未測定 |
| T4 Sketch event | 受付/時間記録のビルド | 未測定 |
| T5 Canvas線分 | 線分/終点のfixture | 未測定 |
| T6 Canvas→Tick | render要求の経路 | 未測定 |
| T7 Canvas→XBM | 変換fixtureと計測点 | 未測定 |
| T8 GFX bitmap/UI | 計測点のビルド | 未測定 |
| T9 submit | 失敗時の現行挙動 | 未測定 |
| T10 executor待ち | active/pending置換fixture | 未測定 |
| T11 SSD1677変換/SPI/BUSY | timing取得経路のビルド | 未測定 |
| T12 sample→Driver完了 | 世代照合/不完全検出 | 未測定 |
| 光学的な線の出現 | Hostで代用不可 | 未測定 |

## D. 検証と判定

Hostの `sketch_trace_core_test`、`sketch_canvas_test`、`sketch_lifecycle_test`、`input_test`、`ft6336_test`、`ft6336_lifecycle_test`、`display_async_test` と、Python解析器の8件の合成ログ試験が通過した。trial/productionのPaperMono firmwareをオフラインでビルドした。trial ELFには `solar_os_sketch_trace_record` があり、production ELFには `solar_os_sketch_trace_*` symbolがないことを確認した。trial `firmware.bin` のSHA-256は `9C737416A39C64077091C25A1AF3DCAAA2ADBD9308597BC929EC9A899BE000BB`、productionは `27F38DB788679033D43BB68C2BED68C68DBD3A2075EF5386A59A0C3E4168A6BA`。ビルド環境に導入されているESP-IDFは **5.5.5** であり、依頼文の5.1.2とは一致しない。検証数値は実機測定の代替ではない。

判定は **CONDITIONAL**。Hostで経路と失敗分類は成立したが、poll位相、FIFO実水位、Canvas/XBM/UIのCPU負荷、SSD1677物理更新、光学的出現、試験版observer負荷は実機未測定。したがって最適化の主因と採用案を今は決められない。計測自体が点の欠落や過大な負荷を起こすならNO-GOとして計測器を直す。

## E. 次の実機段階と改善候補

次段階で別途ユーザーが実機作業を承認した場合にのみ、未保存編集を保存し、PaperMonoのUSBデバイス/portとpartitionを確認して、**application-only** でtrialを投入する。NVS/FAT/otadata/PHYは保持する。USB Shellからjournal開始、Sketch Penで短線・連続線・高速MOVE・一時停止・RELEASEの順に操作し、停止後に全CSVを回収して解析する。overflowとobserver負荷が無視できるかをまず確認し、必要なら固定動画で実表示とDriver完了の差を測る。通常版への復帰もapplication-onlyとし、データ保持を確認する。今回はこの手順の実行をしない。

実測後に検討する案は最大3つに限る。(1) T7/T8が大きい場合にSketchのCanvas変換とUI描画のCPU負荷を削る。(2) FIFO飽和・点欠落が確認された場合に共通Inputへの影響を評価して入力完全性を改善する。(3) Display待ちが支配的な場合に、論理Canvasの全点反映を保ったまま中間提出とPRESS/RELEASE最終frameの優先度を設計する。Writerのburst案やSSD1677波形を転用しない。
