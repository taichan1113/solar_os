# M5Paper MONO UI scale 140% — 実装・受入報告

PaperMonoのShell/TUIと共通GUIに140%スケールを実装し、主要UIの実機受入を完了した。
追加依頼のWriter/Reader z5〜z7、Calcの式・結果25pxも実装・書込・目視確認済み。
その他有効GUIの未目視項目は下表に明記する。全GUI・全状態の実機確認済みとはしない。

- Branch: `feature/papermono-ui-scale-140`
- 開始点: `2a79a8fd`
- 主要実装: `5095918c`（UI140）、`a2efd1b7`（status icons）、`91e99094`（Writer Zoom）、`f13af33f`（Calc25px）、`a45134a5`（Reader Zoom）
- 最新firmwareのsource commit: `a45134a5`。その後の変更は報告書のみ
- 実機: M5Paper MONO / ESP32-S3 / COM39 / MAC `28:84:85:44:79:68`

## 実装方式と互換性

PaperMono manifestだけ `SOLAR_OS_BOARD_UI_SCALE_PERCENT=140` を設定。他ボードは100%。
共通 `solar_os_ui_px()` で位置・余白を最近接整数へ丸める（半分は絶対値の大きい方）。
UIプロファイルは不変データ、RAM scaleはatomic。大規模GUI基盤は追加していない。
Terminal/GFXの描画・文字幅計測を同じ生成済み実効fontへ合わせた。

| 基準font | 140%実効font | advance | ascent/descent |
|---|---:|---:|---|
| 10px | 14px | 7 | 12/3 |
| 12px | 17px | 9 | 15/3 |
| 14px | 20px | 10 | 17/4 |
| 16px | 22px | 11 | 19/4 |
| 18px | 25px | 13 | 21/5 |
| 20px | 28px | 14 | 24/5 |

実機の保存設定は `mono / textsize=20`。NVSの値を28へ書き換えず、実効28pxにする。
mono/compactの違いを保持し、compact20は実効25px。既存font/textsize enumの数値は維持。
GFXの新font IDは末尾へ追加。既存24font配列もバイト同一。他ボードのWriter/Reader上限はz4。
文書・Canvas・PNG・画像・vector座標はUI scaleで機械的に拡大しない。
文書用native font APIは、本文Zoomが二重に拡大されることを防ぐ。

28px Shellは800×480で56列×15行、セル14×29、status高さ34。
セル・行列・cursor・wrap・scrollback・footerを再計算する。
時計・電池・BLE等の全10種のstatus iconと間隔も調整。時計等が小さいとの初回指摘を修正した。

```
setterm uiscale         # RAM上の現在値
setterm uiscale 100     # 従来サイズ
setterm uiscale 140     # 拡大サイズ
```

設定メニューの `uiscale (RAM)` からも切替可能。
再起動するとPaperMonoの140%へ戻る。新しいNVSキーは追加しない。
切替時にTerminal画面・scrollbackを消去し、行列とcursorを再構成する。
Shellコマンド履歴・保存済みfont/textsize・文書は保持。TUI/GUIはresumeで再描画する。

## Font生成

```
make -C fonts/tools/bdfconv
python3 fonts/generate_u8g2_fonts.py
```

既存TTF/bdfconvで17/22/25/28pxのRegular/Bold/Italic/BoldItalicを16配列追加。
ビットマップの単純拡大は用いない。全40配列を別出力先へ再生成し、バイト同一を確認。
既存CMake globでビルドへ登録。Font mapping・4style・幅・baseline・実rasterをテストした。

## 対応画面と実機確認

| 対象 | 140%対応 / layout | 自動検証 | 実機確認 |
|---|---|---|---|
| Shell/Terminal | 実効28px、geometry再計算 | 実font/幅/UTF-8/wrap/scroll/NVS20/100↔140 | 読みやすい、欠け/重なりなし、cursor正常、help等の複数行出力正常 |
| TUI | 共通セルへ追従、設定にRAM scale | menu/list/session/file widgets回帰 | 設定メニュー140%正常・100%従来通り、末尾への選択/scroll確認 |
| Status bar | 高さ・時計・全icon・間隔/fit | 10種100/140の拡大・範囲・100復元 | 最新版の時計/電池/BLEの収まり正常 |
| Writer UI | header34/title25/dialog22・25、実幅fit | GFX/document/checkpoint回帰 | 編集/保存/再読込、header、検索欄/置換開始欄、Shell切替正常 |
| Writer本文 | native、従来z0〜z4維持、追加z5〜z7 | layout/source↔xy/UTF-8/4style/checkpoint | 追加Zoom問題なし、保存z6=25px・本文保持照合PASS |
| Sketch | icon/label縦配置、browser/popup/Cancel | lease/PNG/座標/Canvas往復 | Pen・指位置・保存/再読込・追描画・100↔140・終了正常、Touch自動OFFをログ照合 |
| Calc | 140%で式/結果25px、左panel280px、実幅cursor | GFX幅・font回帰 | cursor修正・重なりなし・拡大サイズOK |
| Launcher | 共通role/実幅へ追従 | launcher layout回帰 | ラベル/矢印選択に異常なし |
| Reader | chrome拡大/fit、本文native、追加上限z7 | document/GFX/既存reader経路回帰 | z7まで欠け/重なりなし、閉じて再読込でz7保持、保存位置照合PASS |
| Web | chrome拡大/fit、本文Zoom従来範囲 | 既存web回帰 | ページを伴う実機目視は未実施 |
| Plot | 文字領域の余白調整、traceは物理座標 | 既存plot回帰 | 入力データを伴う実機目視は未実施 |
| WebRadio/RTSP | header/footer/row/dialog/controls拡大 | 共通media/RTSP layout/transport回帰 | station/streamを伴う実機目視は未実施 |
| Clock/View | vectorはviewportへfit、画像は直接描画 | 既存回帰 | 専用画面の実機目視は未実施 |
| Python/Playground | 共通GFX font解決 | 既存bindings回帰 | 任意scriptの固定座標レイアウトは個別確認が必要 |

writerdeckの有効GUIを現行生成configで調査した。Player/VPlay/Recorder/Funcgen/Invaders/
Synth/Logic/Luaアプリはこの構成では選択されない。共有media chromeは同じscaleを利用。
TUIの全種類、statusのWi-Fi/SD等の全状態、Writerの全dialog状態も個別目視済みとはしない。
長いShell入力・補完・scrollbackの個別操作は自動回帰範囲に含め、今回の個別目視と区別する。

Sketch Canvasは従来どおりx64/y53、736×392。保存PNGの寸法をIHDRでも照合。
Sidebarの幅と操作座標を保持。PNG003/004は各72,617 bytesで再読込・追描画まで正常。
Sketch終了後はmanual OFF・lease0・requested OFF・ready=noを確認した。

## 追加の本文Zoom

| Zoom | native本文font |
|---|---:|
| z0 | 12px |
| z1 | 14px |
| z2 | 16px |
| z3 | 18px |
| z4 | 20px |
| z5 | 22px |
| z6 | 25px |
| z7 | 28px |

PaperMonoのWriter/Readerだけ選択上限を7へ拡張。初期値・既存保存Zoomを自動変更しない。
UI100/140とは独立し、100%へ戻しても本文Zoomを縮小しない。操作は従来の拡大/縮小キー。
WriterはCtrl+Sで保存し、checkpoint saved recordへZoomを記録。旧.metaの読込も維持。
Readerは終了時に既存の位置ファイルへsource anchorとZoomを保存する。

新規 `/writer-zoom-20261011.md` は129 bytes、本文SHA256は作成時と最終確認で同じ:
`0bd6fbb243771469dc61510e6096f7bd8991153136ffeb5bc37a01275200a92f`。
Writerのcheckpointはzoom6・saved flag・checksumを照合。最終計測後もz6を確認。
Readerの対象行は `0 129 z=7 c=0 /writer-zoom-20261011.md`、他5件の位置行は維持。

保存形式のmagic/checksum/schemaや本文形式は変更しない。
旧Writer firmwareはz5以上のmetadata/checkpointを受け付けないため、旧版へ戻す際はz4以下で保存する。
旧Readerは未対応の保存Zoomを復元せず、従来の初期z1を使用する。本文互換性は変わらない。

## 自動検証と性能

最終code `a45134a5` でHost全134 executable PASS、Python578 tests / 4 skip / failureなし。
ビルド94.35秒SUCCESS。生成40fontの再現性、実font/GFX/native、document0〜7、旧heading上限、
checkpoint新Zoom/範囲外拒否、Terminal100/140、status icons、Sketch lease/PNGの回帰を含む。
BLE/Touch/Battery/RTC/I2C/display等も既存全suiteを通過。
Python内のSDK負例assertは期待された子プロセス試験で、suiteの失敗ではない。

Shellは100%と140%で各10文字を追加して、warm描画を比較（入力行全体は同一長ではない）。
再配置full/coldとBackspaceは除外。CPU rasterは設定解決/hash/文字・icon描画を含む。

| Shell | 100%平均 | 140%平均 | 件数/scale |
|---|---:|---:|---:|
| CPU raster | 16.79ms | 21.06ms | 10 |
| display呼出し | 113.35ms | 113.03ms | 10 |

最終firmwareで同じ129-byte文書・native z6のWriterを使い、左右cursorを各scaleで10回動かした。
起動・END・scale再配置・終了は除外。既存debug timestampsでCPUとpanelを分離した。

| Writer / warm cursor移動 | 100%平均 | 140%平均 |
|---|---:|---:|
| CPU clear+render | 7.45ms | 8.18ms |
| submit | 1.45ms | 1.42ms |
| framebuffer変換（2plane合計） | 34.91ms | 34.89ms |
| SPI（2plane合計） | 19.83ms | 19.83ms |
| BUSY | 51.64ms | 51.31ms |
| physical更新全体 | 112.58ms | 112.25ms |
| Writer dispatch→physical完了 | 153.21ms | 167.64ms |

dispatch→完了の範囲は100%で123.51〜182.67ms、140%で124.24〜194.65ms。
非同期frame cadenceの待ちを含み、この平均差全体をfontサイズの影響とは断定しない。
CPU render増分は約0.73ms。GFX headerとnative本文を含む時間で、字形単体のbenchmarkではない。
USB synthetic cursor入力の計測なので、BLEの物理キー押下からの遅延ではない。
BLEからの入力・cursor・表示品質は別途ユーザーの実機操作で正常確認済み。
40MHz SPI・LUT・tile・executorを変えず、転送/BUSY時間はほぼ同じだった。

## データ保護・最終回帰・実機状態

Applicationだけ0x10000へ書込。MAC、実partition table、artifact SHAを照合した。
`erase_flash`は実行しない。NVS/FAT/otadataを全バイトで前後比較し、最新書込でも同一PASS。
書込前の既存43ファイルの一致も確認。ユーザーが以前の本文を削除した編集や、quality文書へ
追加した編集を保持し、過去snapshotへ復元していない。旧PNGやstartup、BLE bondも保持。
ユーザーの新規文書・PNG・Reader位置・Writer checkpointの保存は意図した変更として扱う。

最後のWriter検索/置換欄、時計/電池/BLEの収まり、LED消灯、ZMK OFF→ON再接続は
ユーザーから「全て問題なし」と回答。ログでもBLE close→reconnect→openを確認し、
remembered1/1のまま `version` の入力が正常だった。
Battery3.810V/56%/external、RTCのdate/time読出し正常。Touch OFF、display error=0。
主要Acceptance A〜Fの上表に示す操作範囲を受入済み。その他GUI/状態の目視未実施は上表の通り。

最終状態は140%・mono/base20・orientation270・statusbar show・Shell active。
診断log levelはinfo、timingはOFFへ戻した。計測前後の文書本文も照合済み。
実機はユーザー選択の評価用doseをRAM上で維持。startupは従来のrefresh=fastestのまま。
再起動でdoseはbaselineへ戻る。DC/寿命の正式採用確認はこのタスクでは行っていない。
SSD1677波形・SPI・tile・async executor、BLE/NimBLE、I2C・RTC・Battery・Touch leaseは変更なし。

## 最新Firmware

- Path: `build/papermono-ui-scale-140-trial/firmware.bin`
- Size: **3,429,008 bytes**
- SHA256: `b3827e6dbc4a2d539ed4e9a71fe6c82210b6ff7172eb6490139122e2de242ba5`
- Source: `a45134a5`、board `m5paper_mono`、flavor `writerdeck`、waveform trial define=1
- 書込: `.research/ui-scale/flash-20261011-015724/summary.json`、PASS

旧Writer拡張/Calc版artifactはそれぞれ `.research/ui-scale/artifact-before-calc-f13af33f`、
`artifact-before-reader-a45134a5` に保持。実装・確認の経緯はgit履歴と
`.research/ui-scale/ui-scale-acceptance-history-20261011.md` に保存した。

## 証跡

すべて `.research/ui-scale/` 内。

- `host-reader-zoom.log` / `python-reader-zoom.log` / `build-reader-zoom.log`
- `font-repro.log` / `terminal-140.png` / `status-icons-140.png`
- `shell-perf.log` / `shell-perf-summary.json`
- `writer-ui-perf-20261011.log` / `writer-ui-perf-ring256-20261011.log` / `writer-ui-perf-summary.json`
- `zoom-saved-audit.json` / `final-writer-data-audit-20261011.log`
- `sketch-save-status-20261011.log` / `sketch-scale-roundtrip-20261011.log` / `sketch-exit-acceptance-20261011.log`
- `menu-calc-acceptance-20261011.log` / `calc-launcher-reader-acceptance-20261011.log`
- `reader-z7-acceptance-20261011.log` / `reader-z7-saved-data-20261011.log`
- `final-ui-acceptance-20261011.log` / `final-restored-state-20261011.log`
- `flash-20261011-015724/summary.json` と保護領域のbefore/after binary

途中の「Sketchテストが落ちた」は、実機ではなく会話の回答欄が閉じた問題だったと
ユーザーが明確に説明した。実機障害として数えない。
初回データ読み取りscriptの改行escaping不備と `log show 500` の上限エラーは、
修正scriptと有効上限256で再採取した。これらの失敗ログはPASSの証跡として使わない。

## 変更ファイル一覧

```text
boards/manifests/m5paper_mono.toml
doc/m5paper-mono-ui-scale.md
fonts/README.md
fonts/build/u8g2/u8g2_font_solar_os_default_b_17_tf.c
fonts/build/u8g2/u8g2_font_solar_os_default_b_22_tf.c
fonts/build/u8g2/u8g2_font_solar_os_default_b_25_tf.c
fonts/build/u8g2/u8g2_font_solar_os_default_b_28_tf.c
fonts/build/u8g2/u8g2_font_solar_os_default_bi_17_tf.c
fonts/build/u8g2/u8g2_font_solar_os_default_bi_22_tf.c
fonts/build/u8g2/u8g2_font_solar_os_default_bi_25_tf.c
fonts/build/u8g2/u8g2_font_solar_os_default_bi_28_tf.c
fonts/build/u8g2/u8g2_font_solar_os_default_i_17_tf.c
fonts/build/u8g2/u8g2_font_solar_os_default_i_22_tf.c
fonts/build/u8g2/u8g2_font_solar_os_default_i_25_tf.c
fonts/build/u8g2/u8g2_font_solar_os_default_i_28_tf.c
fonts/build/u8g2/u8g2_font_solar_os_default_r_17_tf.c
fonts/build/u8g2/u8g2_font_solar_os_default_r_22_tf.c
fonts/build/u8g2/u8g2_font_solar_os_default_r_25_tf.c
fonts/build/u8g2/u8g2_font_solar_os_default_r_28_tf.c
fonts/generate_u8g2_fonts.py
packages/solar_os_packages.toml
src/apps/solar_os_calc.c
src/apps/solar_os_player.c
src/apps/solar_os_plot.c
src/apps/solar_os_reader.c
src/apps/solar_os_rtsp_app.c
src/apps/solar_os_sketch.c
src/apps/solar_os_web.c
src/apps/solar_os_webradio.c
src/apps/solar_os_writer.c
src/apps/solar_os_writer_checkpoint.c
src/services/solar_os_doc.c
src/services/solar_os_doc.h
src/services/solar_os_doc_zoom.h
src/services/solar_os_font_face.h
src/services/solar_os_fonts.h
src/services/solar_os_gfx.c
src/services/solar_os_gfx.h
src/services/solar_os_gfx_internal.h
src/services/solar_os_media_widgets.c
src/services/solar_os_media_widgets.h
src/services/solar_os_sessions.c
src/services/solar_os_sessions.h
src/services/solar_os_terminal.c
src/services/solar_os_terminal.h
src/services/solar_os_ui_font.h
src/services/solar_os_ui_scale.c
src/services/solar_os_ui_scale.h
src/shell/solar_os_shell_commands.c
src/shell/solar_os_shell_setterm_tui.c
tests/host/Makefile
tests/host/doc_layout_test.c
tests/host/sketch_lifecycle_test.c
tests/host/terminal_render_test.c
tests/host/writer_checkpoint_test.c
tests/test_status_bar_icons.py
```
