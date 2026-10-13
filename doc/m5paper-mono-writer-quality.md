# PaperMono Writer連続入力・更新履歴比較

Branch: `experiment/papermono-writer-quality`。出発点 `dc136007`。
診断実装commit: `c420acfe860ca7fb99b7ec8cc290a88e73a5490e`。
写真なしの目視評価。実機比較・光学評価は未実施。数値濃度C・保持率Rは未測定。

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
Fastは過去のwarm約112ms/cold約454msとWriter Tick40ms、Hostのburst検証を踏まえ
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

PythonはWSLで592件実行、OK（4skip）。Windowsでは既存試験が`cc`/`make`を
見つけられずUTF-8文書の既定encodingも異なったため、Hostと同じWSL環境で検証した。
trial/production PlatformIO build SUCCESS。production binaryにWriter診断markerがなく、
trialだけに含まれることを確認した。

COM39・ESP32-S3・MAC `28:84:85:44:79:68`、既存app・partition・OTA選択を照合して
`ota_0`のapplicationのみ0x10000へ書き込み、esptoolのwritten-data verification正常。
NVS/FAT/otadata/phy_initは全面backupと書き込み前後比較で全てbyte一致。
証跡: `.research/writer-quality/flash-20261013-094929/summary.json`。
trial: 3,437,664 bytes / SHA256
`edf380f09c465635fe995750c8e1af77a784a6c57d1afa2533dd6c89496af7e1`。
production（未flash）: 3,426,752 bytes / SHA256
`be1a73c50b34ab2c98e2f6a939199cb767e11f03f3f3f8460480257fe54a444a`。
W1は実施済み。W2〜W4は未実施。未確認項目はPASS扱いしない。

### W1の暫定結果とログ出力修正

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
| 波形/入力 | baseline Slow | dose Slow | baseline Fast | dose Fast |
| 入力文字数/物理更新数 | 未実施 | 未実施 | 未実施 | 未実施 |
| 更新間隔/置換回数 | 未測定 | 未測定 | 未測定 | 未測定 |
| 先頭文字の後続更新数 | 未測定 | 未測定 | 未測定 | 未測定 |
| 初回/終了時の濃さ | 未観察 | 未観察 | 未観察 | 未観察 |
| C/R | 未測定 | 未測定 | 未測定 | 未測定 |
| Driver p50 | 未測定 | 未測定 | 未測定 | 未測定 |

H1更新回数、H2更新間隔、H3意図しない画素変更、H4波形履歴はいずれも現状未確定。
4条件のみではH1/H2を完全には分離できない。新規LUT・本番スケジューリング変更、
boot dose採用、追加30/100回試験は今回行わない。
