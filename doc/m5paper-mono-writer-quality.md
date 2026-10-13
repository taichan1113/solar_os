# PaperMono Writer連続入力・更新履歴比較

Branch: `experiment/papermono-writer-quality`。出発点 `dc136007`。
診断実装commit: `c420acfe860ca7fb99b7ec8cc290a88e73a5490e`。
診断ログ修正commit: `8a84bd8bdc3269fb51a3d7d39fdf653e6f8b4c42`。
2026-10-13にW1〜W4をユーザー立ち会いで実施。写真なしの目視評価であり、数値濃度C・保持率Rは未測定。

既存のaより後から追加した文字が濃く見える現象を、両波形・両入力条件で観察した。
追跡した固定参照とaの画素は不変だった。Fastでは実際にpending置換が起こり、10入力が7物理更新へまとまった。
今回の範囲では、更新数・間隔のどちらが薄まりを左右するか、doseが保持に優れるかは未確定。

## Phase 1: 実装の更新経路

1. 通常の入力はInput/BLEからsessionのCHARイベントへ渡る。診断も
   `writer_event(CHAR)` → `writer_handle_char` → `writer_insert`を使う。
2. Gap buffer変更時にversion、parse/layout/render pendingが立つ。
   `solar_os_writer_app.tick_interval_ms`は40ms。CHAR自体は描画しない。
3. TICKで`writer_update_document`がbufferをflattenしMarkdownを再parse、
   カーソルのreveal rangeを含むlayoutを再構築する。
4. `writer_render`は白でGFX全体をclearしheader・全document・カーソルを再raster。
   既存の文字を再描画しても、同じ座標・画素ならold/new差分はない。
   診断ではcursor描画・blinkだけを抑制し、文書のlayout/renderは同じ関数を使う。
5. `solar_os_gfx_submit` → `solar_os_display_submit` →
   `solar_os_display_async_submit`。mutex内でimmutable snapshotへコピーする。
   active slotは不変。pending slotがあると、そのslotだけを最新画像で置き換える。
   submitted generationは各要求で増えるが、置換されたgenerationは実行されない。
6. workerは前のexecute完了後にpendingをactive化する。fastestではtypingの
   frame cadence/idle maintenanceは適用されない。`completed_id`は実行された
   generationに進み、置換されたIDを飛ばす。IDの差を更新回数に使ってはいけない。
7. board/service executeから`epd_ssd1677_present_snapshot`に渡されたframeを使用。
   fastestのtile変換はnew snapshotを0x24、最後の正常completed shadowを0x26へ
   全面転送する。SPIは40MHz。LUTは既存baseline/dose。
8. SSD1677 BUSY完了後にだけsnapshotをshadowへ昇格する。転送/BUSYエラー時は
   shadow無効・fault latch・pending破棄となる。診断はfault後cleanupを再試行しない。

主なファイル: `src/apps/solar_os_writer.c`、`src/services/solar_os_doc.c`、
`src/services/solar_os_gfx.c`、`src/services/solar_os_display.c`、
`src/services/solar_os_display_async.c`、`src/drivers/epd_ssd1677.c`、
`src/drivers/epd_ssd1677_reference.inc`。

## Phase 2: 診断方針

trial限定serialコマンド:
`display test display0 writer-quality baseline|dose slow|fast`。
既存Writerが開いていれば拒否する。専用contextとapp lifecycleでRAM文書を構築し、
checkpoint open、metadata、recovery、保存を行わない。

初期化の白とWriter固定参照は正規OTP cleanup。初期化fast更新は0回。
共通参照は`FIXED REFERENCE`と`The quick brown fox jumps over the lazy dog`。
z4・20px本文、R2・800×480、40MHz・tileで`abcdefghij`を追加する。
Slowは各submitのcompleted fenceを確認してから次のCHAR。
Fastは既存のwarm約112msとWriter Tick40ms、Hostのburst検証を踏まえ
60msを選ぶ。80msでは3文字の描画が40/80/160msとなり、112msの更新に対し
置換が起きない場合があった。60msなら40/80/120msとなる。
実測入力間隔・実際の置換を記録して、期待どおり統合したか判定する。

白、共通参照、最初のa、abcd、abcdefg、abcdefghijでoperator gate。
最初のaはFastでも完了を待ち、独立した初回濃さを観察してから後続入力を開始する。
各gateはUARTのgで1区間だけ進む。qは中止。4条件を自動連続実行しない。
gate応答時刻と最終更新完了からの経過を記録するため、観察待ちを短い更新間隔と
混同しない。途中gateによりFast試験も3文字ごとのburstとなる。

最大10件のfast submitを**submit前**に予約・制限する。各snapshotは最大1物理更新
なので、active/pending/置換を含めた上限は実物理更新10回以下となる。
さらにdriver入口のtrial gateも11回目をハードウェア駆動前に拒否する。
入力数による上限だけに頼らない。実際のactivation回数は別に記録し、足りない更新を
補わない。変換、SPI、BUSYの時間は既存driver計時を読む。driverのtrial observerで
fixed/first ROIと変更画素をRAMに記録し、そのobserver所要時間も報告する。
測定区間ではDEBUGログを抑え、journalは停止後にUSBへ出力する。

正常終了ではOTP cleanup、元の波形/refresh/timing/log levelの復元、claim解放、
foreground Terminalのrender cache全体のinvalidateを行う。試験画像がShellに残る
前回の問題を避ける。fault時は新たな物理更新を開始しない。

## 検証・実測

Host回帰137実行PASS。新規fixtureは実Writerの編集・Markdown/GFX/U8g2 rasterと
SSD1677のSPI捕捉を用い、両planeを独立scalar参照へ全画素照合した。
全更新で固定ROIの保持黒00・保持白11、最初のaの座標/画素保持を確認した。
Slow/Fastの待機・置換境界は決定論的モデルを使用し、実executorのpthreadテストは
既存`display_async_test`で別途PASS。モデルの10入力はSlowで10物理更新、
Fastで7物理更新/3置換となった。この値は**実機測定値ではない**。
中止、BUSY timeout、未完了shadow非昇格、driver入口11回目拒否、通常画面復帰もPASS。

Pythonは最終修正後にWSLで593件実行、OK（4skip）。新規Python8件もPASS。
ログ修正後の実Writer Host fixtureもPASS。Windowsでは既存試験が`cc`/`make`を
見つけられずUTF-8文書の既定encodingも異なったため、Hostと同じWSL環境で検証した。
trial/production PlatformIO build SUCCESS。production binaryにWriter診断markerがなく、
trialだけに含まれることを確認した。

COM39・ESP32-S3・MAC `28:84:85:44:79:68`、既存app・partition・OTA選択を照合して
`ota_0`のapplicationのみ0x10000へ書き込み、esptoolのwritten-data verification正常。
NVS/FAT/otadata/phy_initは全面backupと書き込み前後比較で全てbyte一致。
証跡: `.research/writer-quality/flash-20261013-094929/summary.json`、
ログ修正版の`.research/writer-quality/flash-20261013-101132/summary.json`。両書き込みとも保護領域は前後一致。
W1前後のFATをファイル単位でも照合し、40ファイルのうち変化はShell履歴だけ。
保存文書・metadata・recovery等は一致。NVS/FATのraw imageは実行中のShell履歴等で変わるため、
「書き込み前後の保護」と「診断実行中の論理ファイル保持」を区別した。

実機の最終trial: 3,437,312 bytes / SHA256
`59e90f10389dd261a98d362e901e175739334c3623e3dd390f06c9cfa5d1721b`。
最終production（buildのみ・未flash）: 3,426,624 bytes / SHA256
`d9c05191760b33cca70ddab0d85a0bde84290d96e8ff1c628b80950b36b23221`。
trial/productionは最終sourceでそれぞれ126.22秒/125.11秒のbuild SUCCESS。
W1は初版trial、W2〜W4はログ修正版trialを使用。差は測定後の出力形式だけで、測定経路は同じ。

### W1の計時欠落とログ出力修正

W1は10文字入力・10 Render/submit・10物理更新・置換0で正常終了した。
ユーザーは初回のaを濃く読みやすいと確認し、4文字時点ではa/bcdに大きな濃さの違いなし、
7文字時点ではaと固定参照の薄まりを感じ、a→gに濃さのグラデーションがあると回答した。
10文字時点は7文字時点からあまり変わらず、jがaより濃い。背景は正常。
固定ROIは全更新で差分0・保持黒703画素・保持白6797画素、最初のaは後続更新で差分0・保持黒47画素。

実機Shellのprintfは192バイトの固定バッファであり、長いframe/physical行の末尾が切れた。
記録の先頭部分とsummaryは回収できたが、driver詳細計時・SPI転送数等の末尾は欠落した。
W1のdriver p50、BUSY、変換、SPI時間は未測定として扱い、完全なログ検証PASSにはしない。
追加更新はせず、以後の診断出力だけを短い断片に分割して同一行を組み立てる。
Hostの出力fixtureも実機と同じ192バイト上限を検証し、欠落ログの解析は失敗理由を返す。
この修正は測定後のログ出力のみで、入力・render・executor・LUTの動作を変更しない。

計時の限界: parse/layoutは一つの処理区間で記録し、render要求時刻はCHAR処理終了時の
pending確認時刻。frameの対応は10件以内のnative FNV1a識別を使い、解析時に重複や
generation不一致を拒否する。observer overheadはdriver totalと別に記録する。
平均更新間隔にはoperator gate待ちが含まれるため、burst内の間隔も別に集計する。
本番の周期・波形・cursor・checkpointは変更していない。trial診断中だけ、display0の
所有権を基準にmainの通常イベント/Tickを止め、外部KEY1/keyboard/touchを消費する。

| 指標 | W1 | W2 | W3 | W4 |
|---|---|---|---|---|
| Waveform | baseline | dose | baseline | dose |
| 入力モード | Slow | Slow | Fast | Fast |
| 入力文字数 | 10 | 10 | 10 | 10 |
| Render / Present要求数 | 10 / 10 | 10 / 10 | 10 / 10 | 10 / 10 |
| 物理更新回数 | 10 | 10 | 7 | 7 |
| pending置換回数 | 0 | 0 | 3 | 3 |
| 先頭aの後続保持更新数 | 9 | 9 | 6 | 6 |
| 全区間平均更新間隔・観察待ち込み | 22.596秒 | 34.534秒 | 25.065秒 | 41.788秒 |
| burst内平均更新間隔 | 175.704ms | 175.797ms | 126.958ms | 126.929ms |
| burst内実入力間隔の範囲 | 175.0〜176.8ms | 175.0〜176.7ms | 60.0〜61.0ms | 60.0〜60.8ms |
| 最後に入力したjから最終完了まで | 174.045ms | 176.011ms | 180.983ms | 182.009ms |
| 初回aの目視 | 濃く読みやすい | 濃く読みやすい | 濃く読みやすい | 濃く読みやすい |
| 終了時の目視 | jがaより濃い | j→aにかけて薄い | 7文字時点とほぼ同じ | abcde付近まで同程度、その先は濃くなる印象 |
| 数値濃度C / 保持率R | 未測定 / 未測定 | 未測定 / 未測定 | 未測定 / 未測定 | 未測定 / 未測定 |
| Driver p50 | 未測定・ログ欠落 | 112.190ms | 113.640ms | 113.571ms |
| BUSY p50 | 未測定 | 51.632ms | 51.528ms | 51.545ms |
| 両plane変換 p50 | 未測定 | 34.454ms | 35.693ms | 35.520ms |
| 両plane SPI転送 p50 | 未測定 | 19.795ms | 19.812ms | 19.802ms |
| ROI observer p50・driver外 | 未測定 | 11.697ms | 11.832ms | 11.886ms |
| 全journal検証 | 不完全・計時欠落 | PASS | PASS | PASS |
| 正常終了 / Shell目視復帰 | 正常 / 正常 | 正常 / 正常 | 正常 / 正常 | 正常 / 正常 |

全区間平均はoperator確認待ちを含み、入力速度や処理速度の指標には使わない。
burst内平均は各bcd/efg/hij区間だけの、物理更新開始→次の開始の間隔。
各区間は観察のため停止するので、10文字全体を無休止で打つ試験とは異なる。
最初のaは全条件で独立して表示完了を待ち、最初のdriver呼出開始→完了は約453〜456msだった。
初回はwarm更新と区別する。上表のdriver時間はobserverを含まない。
observerは約11.7〜11.9msを追加し、通常版の更新間隔・pending置換数まで同一と保証できない。
parse/layout p50は約0.50〜0.52ms、Render p50は約4.16〜4.23ms。

Slowの物理完了画像は1,2,3,4,5,6,7,8,9,10文字。
Fastの物理完了画像は1,2,4,5,7,8,10文字で、3,6,9文字のpending画像が各1件置換された。
入力を破棄したのではなく、完成画像に全10文字を含む。最終内容は全条件`abcdefghij`。
全4条件で合計34回のfast更新、各条件10回以下。足りない更新の追加や30/100回への延長はしていない。

### 画素と目視の比較

固定ROIは`FIXED REFERENCE`の領域（論理x=4,y=37,w=250,h=30）。
初期OTP参照から全34物理更新で差分0、保持黒00=703・保持白11=6797画素。
最初のaは論理x=4,y=147,w=10,h=22で、初回黒化47画素。
その後の全保持更新で座標不変、差分0、保持黒00=47。
カーソルは診断限定で無効、スクロールなし。同じ既存画素を再rasterしても黒→白には変わっていない。
これは実機のshadow/new snapshot比較。実SPI両planeの独立scalar照合はHostで確認しており、
実機SPIを別測定器で捕捉したものではない。

4文字時点では全条件で大きな濃さの変化なし。7文字時点では全条件で古いaより新しい文字が濃く見えた。
W1はaと固定参照の初回からの薄まりも報告。W2の最終観察では固定参照がaより少し濃い印象。
W3は7文字時点で固定参照がaと同程度、最終観察は7文字時点とほぼ同じ。
W4の最終観察は「差はあまりわからない」「abcdeぐらいまでは同じ濃さで、そこからfghijはだんだん濃くなってる気がする」。

W1対W3・W2対W4では更新数と区間内間隔の両方が変わったが、Fastの保持優位性は確認できなかった。
W1対W2・W3対W4でもdoseの明確な保持優位性は確認できなかった。
同じ文字数でも更新履歴が異なることは実測で確認できたが、光学的な同等性を証明したものではない。
a〜jは文字形状も異なるので、横並びの濃さの差だけではaの保持率にならない。
写真・定量濃度・温度の記録はなく、初回の同じaとの目視比較とユーザーの原文を区別して残した。

観察待ちは実機gate応答時刻と最後の物理完了の差を記録した。
これは観察回答・PC操作・通信を含む代理指標で、目が画面を見た瞬間の時刻ではない。

| 回答gate | W1 | W2 | W3 | W4 |
|---|---:|---:|---:|---:|
| 初回a | 35.562秒 | 185.502秒 | 42.682秒 | 125.762秒 |
| abcd | 67.342秒 | 39.842秒 | 46.421秒 | 66.941秒 |
| abcdefg | 98.542秒 | 83.542秒 | 60.041秒 | 56.781秒 |
| abcdefghij | 93.782秒 | 96.422秒 | 103.921秒 | 143.741秒 |

### 原因候補の判定

| 仮説 | 判定 | 根拠と限界 |
|---|---|---|
| H1 物理更新回数の影響 | 現状では未確定 | 後続更新を経験した古い文字が薄く見える観察あり。ただし10回対7回で保持差を明確に確認できず、回数単独の効果は未特定。 |
| H2 更新間隔の影響 | 現状では未確定 | 約176ms対127msを実測したが、同時に回数も変化。観察待ちも不均一で、間隔だけに帰属できない。 |
| H3 既存文字の意図しない画素変更 | 否定する実測あり・診断ROI内 | 固定参照とaの座標・画素が不変で、後続駆動時は00/11保持。通常カーソルや長文スクロールまで否定するものではない。 |
| H4 baseline/doseの履歴依存性差 | 現状では未確定 | 両方で新しい文字が濃く見える。同一入力条件の比較でdoseの優位性を確認できず、C/Rも未測定。 |

最も絞れた点は、診断ROIの薄まりを既存文字の再rasterによる白画素化では説明できないこと。
物理表示の履歴が候補として残るが、その回数・間隔・駆動配分のどれが支配的かは特定していない。

### 次に検討する改善候補（今回未実装）

原因の確度が十分でないため、以下は候補。小さい回数上限・同一初期状態で効果を確認してから本番採用を判断する。

| 優先 | 案 / レイヤー | 期待効果 | 速度への影響 / 安全性 |
|---|---|---|---|
| 1 | Apps / Display servicesで、初回応答を維持した短い入力burstの統合と不要更新削減を検討 | 既存pending置換で10→7回になった実績を基に、保持画素が更新を経験する回数を減らす。濃さ改善は未証明。 | まとめる待ち時間で初回応答を悪化させない検証が必要。LUT・電圧変更なし。入力内容を失わず、最終snapshotを必ず完了する。 |
| 2 | 診断で更新回数を揃えた間隔比較を行い、効果があればDisplay servicesの最小更新間隔を検討 | H2を切り分け、少し遅くして保持が改善する条件を探す。 | cadenceを延ばす分だけ追従が遅くなる。回数を増やして帳尻を合わせず、エラー時の追加更新を避ける。 |
| 3 | Driver側の保持00/11を含む波形履歴を、初回黒化と分けて評価 | 画素が不変でも見た目が変わる現象を、黒化不足と保持劣化に分ける。 | 新LUTや電圧は今回は変更しない。駆動配分を変える場合はDC・寿命の根拠が必要。現結果だけでdoseを正式採用しない。 |

### 成果物・変更範囲

- Apps: `solar_os_writer.c/.h`、`solar_os_writer_quality.inc`、`solar_os_writer_quality_trace.h`。
- Drivers: `epd_ssd1677.c/.h`のtrial observerのみ。変換・SPI・LUT値は非変更。
- 統合: `src/main.c`のtrial中イベント抑制、`src/shell/solar_os_shell_commands.c`の診断dispatch。
- Tests: `tests/host/Makefile`、`writer_quality_test.c`、`tests/test_writer_quality.py`、`tests/hil/capture_writer_quality.py`、`analyze_writer_quality.py`。
- [集計CSV](m5paper-mono-writer-quality-metrics.csv)、[イベント・目視回答JSON](m5paper-mono-writer-quality-results.json)。JSONにはW1の欠落理由も残した。
- rawログ・build・保護領域比較: `.research/writer-quality/`。個人データを含み得るbackupはGitへ追加していない。

全条件の正常終了とShell復帰をユーザーが確認。最終USB queryでShell sessionだけが残り、`refresh=fastest`を確認した。
実機はtrial評価版のまま、診断前のRAM dose選択へ復元。boot waveformはbaselineのまま。
本番Writer・async executor・LUT・SPI40MHz・tile・UI/zoom・BLE/Touch/RTC/電源仕様は非変更。
production buildでは診断が除外される。追加の未承認改善や更新延長は行っていない。
