# PaperMono Writer 更新統合の Host 再評価と Sketch Phase 0

2026-10-13。対象は `experiment/papermono-writer-burst-coalescing` の C0/C1 各1回の実機 journal と現行ソース。今回の作業は Host 調査のみで、ファームウェア、実機、保存文書、表示設定を変更していない。元の試験・動画解析は [前報](m5paper-mono-writer-burst-coalescing.md) を参照する。

## 判定

**Writer の burst 統合は NO-GO。** 候補を3案に限って評価したが、C0 より実物理更新を減らしながら最大遅延・最終文字遅延をともに約30ms以内に保ち、Tick 位相と小さな入力揺らぎにも耐える案はなかった。`160ms/80ms` の C1 trial は本番へ移さず、この Writer 方向の追加パラメータ探索を止める。通常 Writer の既存方針と実機上の trial はそのまま維持する。Sketch へは別の問題として移り、今回はコード調査と測定設計までとする。

## C0/C1 実測の再生とモデル

基準は `C0/analysis.json`、trial は `C1/analysis.json`（いずれも `.research/writer-burst-coalescing/` 配下）。形成済みの `a` 20字のあと、`bcdefghij` 9字を約60ms間隔で送った記録を使う。表の物理更新には形成時の1回を含む。遅延は CHAR 送信から、その文字を初めて含む Driver 完了までであり、光学的な文字出現時刻ではない。

| 観測 | C0 通常 | C1 160ms/80ms |
|---|---:|---:|
| 入力 / render 要求 / 物理更新 | 29 / 10 / 6 | 29 / 5 / 5 |
| 後続9字の物理 snapshot 文字数 | 21, 23, 25, 27, 29 | 21, 25, 28, 29 |
| pending 置換 | 4 | 0 |
| 後続先頭 / p50 / p95＝最大遅延 | 164.197 / 164.197 / 220.496ms | 162.859 / 202.080 / 302.608ms |
| 最終文字 / 最大無更新区間 | 148.976 / 164.197ms | 241.012 / 200.437ms |

`tests/hil/model_writer_feasibility.py` の**実測 trace replay**は input 順序、各 submit の世代・文字数・hash、Driver が採用した最新 pending と active、置換数、最終内容を照合し、上記の response を誤差0で再計算した。これは記録済み submit/Driver 時刻を使った整合性確認であり、予測精度の検証ではない。

別の**予測 scheduler**は CHAR 登録、40ms Tick、render/submit、active＋最新 pending、Driver 完了、immutable snapshot を離散イベントとして再構成した。C0/C1 の実測 Tick と入力時刻を与え、両試行から得た中央値（入力登録188µs、Tick→submit 7.293ms、worker 起動0.850ms、温時 Driver 114.037ms）を使用した。温時 Driver の実測幅は112.773～116.154ms。形成時の物理1回は予測の外で加算する。

| 対象 | 後続物理 / 置換 | 先頭 | p50 | 最大 | 最終 | 実測との最大差 |
|---|---:|---:|---:|---:|---:|---:|
| C0 実測 | 5 / 4 | 164.197 | 164.197 | 220.496 | 148.976 | — |
| C0 予測 | 5 / 4 | 163.553 | 163.553 | 217.688 | 142.346 | 6.630ms |
| C1 実測 | 4 / 0 | 162.859 | 202.080 | 302.608 | 241.012 | — |
| C1 予測 | 4 / 0 | 163.491 | 202.776 | 302.662 | 242.202 | 1.190ms |

単位は遅延列が ms。両経路の snapshot 列も一致した。ただし、この適合は同じ2試行の時刻・測定作業時間を入力にしており、未知の入力や光学遅延への外挿ではない。

前の簡略 Host 見積もりは C1 最大約254ms、最終約154ms とした。実測の約303/241msとの差には、40ms Tick の位相、約7ms の render/submit、約0.85ms の worker 起動、温時 Driver 時間の変動、CHAR→方針登録の約0.19ms が効く。とくに最後の文字は送信後の登録が約480.88ms、次の Tick が約560.72msで、登録からは約79.84ms。80ms idle 境界の直前なのでこの Tick では最終描画にならず、約40ms 後の Tick へずれた。さらに active/pending の物理更新待ちが残る。固定112msの Driver と「最終文字は即座に trailing flush」という旧仮定ではこの経路を表せなかった。境界の数百µsを一般的な保証値として扱わない。

## 3案だけの Host 評価

比較の基準は同じ Tick と入力時刻で動かした C0 予測。次表は C0 記録の Tick 位相での結果で、物理回数には形成時の1回を加えた。先頭・最大・最終の単位は ms。

| 案 | 方針 | 物理回数 | 先頭 | 最大 | 最終 | 判定 |
|---|---|---:|---:|---:|---:|---|
| C0 予測 | 更新統合なし | 6 | 163.553 | 217.688 | 142.346 | 基準 |
| A | 120ms周期、80ms idle | 5 | 163.553 | 262.791 | 162.969 | 最大 +45.103ms |
| B | 160ms周期、40ms trailing | 6 | 163.553 | 217.688 | 142.346 | 物理回数が減らない |
| C | pending 時の render 抑止 | 7 | 163.553 | 322.352 | 257.233 | 回数・遅延とも悪化 |

4種の Tick 位相 × 温時 Driver の実測上下限で各8通りを調べた。60ms刻み9字では A の物理回数は常に5回だが、最大遅延増は +26.0～46.4ms で GO 条件を満たすのは2/8。±3msの交互揺らぎでは最大増 +32.0～46.4ms、0/8。短い休止を挟む系列では最大増 +23.0～46.4ms、最終増は最大 +46.4ms。100ms刻みでは A も物理回数を減らせない。B は60ms系列で基準と同じ6回、C は7回となった。単発、100ms間隔、途中休止、Backspace/改行の最終 snapshot はモデル内で確認した。位相4点と作業時間2点は代表条件であり、網羅的な確率分布や実機確認ではない。

既存 Host の `writer_quality_test` は実アプリの編集・raster・pending置換・上限・abort/fault を、`display_async_test` は停止・fault・pending破棄を検査して通過した。今回の候補は C コードに実装していないため、候補固有の保存、電源停止、オーバーフロー、故障復帰、見た目の品質は **未検証**。理論上の最終 snapshot 成立だけを実機 PASS と読まない。C0/C1 の光学比較も元動画1試行ずつで、C1 の個別更新遅延は測れず、濃度比の差は測定揺らぎに対して小さかった。品質改善を採用理由にはできない。

再現手順（元 journal がある環境）:

```sh
python3 tests/hil/model_writer_feasibility.py \
  --c0 .research/writer-burst-coalescing/C0/analysis.json \
  --c1 .research/writer-burst-coalescing/C1/analysis.json \
  --output .research/writer-burst-coalescing/feasibility-host.json
python3 -m unittest tests.test_writer_burst tests.test_writer_feasibility -v
```

元 journal はローカル `.research` に保持し、今回の commit には含めない。欠けている checkout では実測依存の新テストは skip する。

## Sketch Phase 0：独立した調査

Writer の CHAR/Tick 統合を Sketch に転用しない。Sketch の入力単位は押下、移動、離上で、**届いた移動点を論理 Canvas の連続線分へ反映すること**と、途中画面をどの頻度で E-paper に提出するかを分けて扱う。

現行経路は `GT911` worker の約10ms poll → I2C の現時点座標・回転 → pointer FIFO（容量32、満杯時は書込失敗）→ main loop の8件ずつの dispatch → Sketch の PRESS/MOVE/RELEASE → Canvas line/shape → 40ms App Tick → Canvas の全域 XBM 変換と GFX/UI描画 → `solar_os_gfx_submit` → active＋最新 pending の非同期 executor → SSD1677。main loop は入力を Tick 前後にも読み出す。**観測前の移動経路**（10msサンプル間）は復元できない。一方、成功裏に届けられた Pen MOVE は前回点からの線分として Canvas に保持されるため、表示用 pending の置換をそのまま論理線分欠落とは呼べない。shape の MOVE は preview、RELEASE で確定する。FIFO 満杯時と RELEASE の最終提出を別々に追跡する必要がある。現在の `sketch_render()` は submit エラー後も `render_pending` を消すため、fault 時の可視最終状態も未検証の観測項目とする。

過去の Sketch 実機では指と線の位置が合い、追従に多少のラグがあった。既報の約0.916～1.128秒の更新は `refresh=partial` の一時ログであり、現在の `fastest`・Pen 終端遅延に当てはめない。混合操作での最大 session tick 約405msと期限超過189回も Pen 単独の因果値ではない。Canvas 変換、全画面 UI 再描画、submit 待ち、Driver、光学変化の内訳はまだ未測定である。

次段階の測定設計は、同じ一本の stroke ID と点 sequence を入口から最終画面まで維持する。GT911 poll 完了、FIFO投入可否・水位、main dispatch、Sketch の点受信/Canvas version、各 Tick の開始終了、XBM変換・全画面UI描画、submit世代と snapshot hash、pending置換、worker開始/終了、Driver SPI/BUSY、動画の実表示時刻を記録する。PRESS直後の先頭線分、連続 MOVE、短い停止、RELEASE の最終線分を別に測り、点の**取得損失**と表示 frame の**意図した省略**を混同しない。最終 Canvas と保存/再読込の内容一致、Shell切替、停止・復帰、fault/queue full も独立に検証する。USBログだけで光学的追従性や薄まりを PASS としない。

測定前の改善仮説は最大3種類に限る。(1) 全Canvas変換/UI描画の CPU 時間を減らす、(2) pointer FIFO の飽和・取りこぼしを防ぎつつ全 MOVE/RELEASE を論理 Canvas に残す、(3) 中間表示の submit を抑えて先頭と離上後の最終 frame を優先する。まず計測で主因を定め、実装層と閾値を選ぶ。SSD1677 波形や Driver を先に変更しない。

### 次タスク用プロンプト案

> PaperMono Sketch の Pen 描画追従を対象に、GT911 poll から論理 Canvas、描画 Tick、非同期表示、SSD1677 完了、動画上の出現までを同一 stroke/point/snapshot ID で計測してください。先に Host で PRESS/MOVE/RELEASE の順序・FIFO満杯・pending置換・最終 Canvas 保持を検証し、実機操作が必要になる段階ではデータ保持と application-only 書込条件を確認してください。入力点欠落と中間 frame 省略を別指標にし、先頭線分と RELEASE 最終線分の遅延、Canvas raster/UI/submit/Driver の各時間、queue 水位、目視の追従・濃さを記録してください。測定で支配要因を確かめるまでは最適化を実装せず、候補は3案以内で GO/NO-GO を示してください。Writer の burst 方針や SSD1677 波形を Sketch へ転用しないでください。

## 検証と状態

`tests.test_writer_burst` と新規 `tests.test_writer_feasibility` は計13件 PASS。Host の `writer_quality_test`、`display_async_test`、`sketch_canvas_test`、`sketch_lifecycle_test`、`input_test` は PASS。新たな Sketch 実機計測、Writer 候補の実機 A/B、光学的な低遅延保証は行っていない。今回の変更はこの報告と Host ツール/テストのみ。
