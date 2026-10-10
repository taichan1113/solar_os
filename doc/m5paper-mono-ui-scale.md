# M5Paper MONO UI scale 140%

実装・自動検証を完了。実機の目視Acceptanceは現在進行中で、未確認項目をPASSにしていない。
ブランチ: `feature/papermono-ui-scale-140`、開始点: `2a79a8fd`。

## 設計と保存互換性

PaperMonoだけ `SOLAR_OS_BOARD_UI_SCALE_PERCENT=140` を設定。他ボードは100%。
論理UI寸法を `solar_os_ui_px()` で整数へ丸める（最近接、半分は絶対値の大きい方）。
Terminal/GFXは同じ生成済み実サイズフォントを解決し、文字幅計測と描画を一致させる。
RAM上のスケール値はatomic、UIプロファイルは不変データ。

| 論理サイズ | 140%実効サイズ | advance | ascent/descent |
|---|---:|---:|---|
| 10 | 14 | 7 | 12/3 |
| 12 | 17 | 9 | 15/3 |
| 14 | 20 | 10 | 17/4 |
| 16 | 22 | 11 | 19/4 |
| 18 | 25 | 13 | 21/5 |
| 20 | 28 | 14 | 24/5 |

実機の保存設定は `mono / textsize=20`。値を28へ上書きせず実効28にする。
`compact`は従来の小さいfaceを選んでからスケールするため、compact20は実効25。
既存font ID、textsize enum、NVSのfont/textsizeキーは変更しない。新しいNVSキーも追加しない。
28pxのShellは800×480で56列×15行、セル14×29、status高さ34。

```
setterm uiscale         # 現在値
setterm uiscale 100     # 100%へ戻す
setterm uiscale 140     # 140%へ戻す
```

切替はRAMだけ。再起動するとPaperMonoの140%へ戻る。
切替時は各描画Terminalの画面・scrollbackを消去し、行列数とカーソルを再構成する。
Shellコマンド履歴、文書、保存済みfont/textsizeは保持する。TUI/GUIはresumeによって再描画。
設定TUIの `uiscale (RAM)` からも切替可能。他ボードで140を指定するとunsupported。

## フォント生成

```
make -C fonts/tools/bdfconv
python3 fonts/generate_u8g2_fonts.py
```

既存TTF/bdfconvを使用。17/22/25/28pxのRegular/Bold/Italic/BoldItalicを16配列追加。
従来の24配列はバイト同一。全40配列を別出力先へ再生成してバイト同一を確認。
生成配列は既存のCMake globで登録される。ビットマップの拡大処理は追加しない。

## 対応画面と検証範囲

| 対象 | 対応内容 | 自動検証 | 実機確認 |
|---|---|---|---|
| Shell/Terminal | 保存値から実効font、セル/行列/折返し/footer再計算 | 実font raster/幅/UTF-8/wrap/scroll/NVS20読込/100↔140 | 28px・欠け/重なりなし・cursor PASS、切替回答待ち |
| TUI | Terminalセルへ追従。設定にRAM scale追加 | 既存menu/list/session/file widgets回帰 | 待ち |
| Status bar | 高さ・clock font・全アイコン・間隔 | 各状態raster、全10種100↔140拡大と描画範囲 | 初回サイズ不揃い指摘、修正後確認待ち |
| Writer UI | header34、title25、dialog22/25、実幅による入力切詰め | GFX native font/文書layout/保存/recovery/checkpoint回帰 | 待ち |
| Writer本文 | native font APIで従来zoom/Markdown layoutを維持 | 文書layoutとnative幅、zoom enum不変 | 待ち |
| Sketch | icon/label縦配置、browser行/Cancel/popup拡大 | 140%lease/PNG保存再読込/座標/キャンバス100%往復 | 待ち |
| Calc | 実font metricsによる行高/選択行表示/実幅cursor | GFX widthと既存calc回帰 | 待ち |
| Launcher | 既存role profileと実幅レイアウトへ追従 | launcher layout回帰 | 待ち |
| Reader/Web | chrome拡大/切詰め。文書zoomはnativeで保持 | 既存reader/web回帰 | 待ち |
| Plot | 文字領域の余白を確保。traceデータは物理座標 | 既存plot回帰 | 待ち |
| WebRadio/RTSP | header/footer/row/dialog/controlsを拡大 | 共通media/RTSP layout/transport回帰 | 待ち |
| Clock/View | Clockは画面に合わせたvector digits、Viewは画像。直接拡大不要 | 既存回帰 | 待ち |
| Python/Playground | 共通GFXのUI fontを解決。スクリプトが指定する固定座標は自動拡大しない | 既存bindings回帰 | 任意スクリプトの配置は個別確認が必要 |

Writerdeck生成configで有効なGUIを調査した。Player/VPlay/Recorder/Funcgen/Invaders/
Synth/Logic/Luaアプリはこの構成では選択されない。共有メディアchromeは同じスケールを使う。
Reader/Webの文書独自zoom、GIF/PNG/Canvas/vector座標は自動拡大しない。
Sketchキャンバスは従来どおりx=64、y=53、736×392、保存PNGの寸法も維持。

## 回帰と実機データ保護

Host: 134 executables。Python: 577 tests / 4 skip。最終結果とartifactは後段に記録する。
既存43ファイルは書込み前SHA256が従来snapshotと一致。
`/quality-dose-speed-20261010.md` は前回5文字から追加編集されていることを読み取りで確認。
現在内容を保護し、過去snapshotの内容へ戻さない。

Application-only flashでMAC `28:84:85:44:79:68`、実partition table、artifact SHAを照合。
書込offsetは0x10000、ota_0最大0x700000。NVS/FAT/otadataは前後の全バイトを比較する。
`erase_flash` は実行しない。

SSD1677 LUT、dose/hold、SPI40MHz、tile変換、async executor、BLE/NimBLE、RTC/Battery、
I2C、Touch lease、文書/PNG保存形式は変更していない。
評価用doseは再起動でbaselineへ戻ることを事前説明。書込後RAMだけでdoseを再選択し、
startupのrefresh=fastestは変更しない。DC/寿命未確認という前回の評価条件も維持。

## 証跡

- `.research/ui-scale/host-final.log`
- `.research/ui-scale/python-final2.log`
- `.research/ui-scale/build-final2.log`
- `.research/ui-scale/font-repro.log`
- `.research/ui-scale/terminal-140.png`（実U8g2レンダー確認）
- `.research/fastest-quality/ui-scale-preflash-audit.log`（既存43一致、評価文書の変更を検出）

目視Acceptance A〜F、CPU表示計測、書込前後のデータ保持、最終artifact識別の結果は
実機確認後に追記する。現時点で全UI対応完了/受入完了とはしない。

## 書込済みartifactと自動検証の確定結果

実装commit: `5095918c`。Host全134 executable PASS、Python577 test、4 skip、失敗なし。
Font全40配列の再生成バイト一致PASS。ビルド最終37秒（初回再構成253秒）。

- Firmware: `build/papermono-ui-scale-140-trial/firmware.bin`
- Size: 3,425,328 bytes
- SHA256: `ac74df37bb5daada36522206a8691df9f3b36df3bc41c041a796a70ac8f1579d`
- Source: 5095918cと同内容のcode（ビルドはコミット直前）
- Flash: `.research/ui-scale/flash-20261011-002922/summary.json`、PASS
- NVS/FAT/otadata: 書込前後の全バイト一致
- Boot: scale140、orientation270、mono、textsize20、statusbar show
- BLE: travelers-split接続、remembered1/1保持
- RTC: date/time読出し正常、Battery3.804V/56%/external
- 評価用dose: 起動後RAMだけで再選択済み

Shell目視回答: 28pxは読みやすく、文字欠け・行の重なり・カーソルのずれなし。
100↔140比較操作は依頼済みだが、往復結果の個別回答はまだない。
上部バーのアイコン、特に時計が小さいとの指摘を受け、SD/音声/radio/link/clockの
未拡大部分とキーボード内の間隔を修正。全10種の100↔140描画範囲・拡大を
追加テストしPASS。修正版の目視はこれから確認する。

修正版Host全134 executable PASS、Python578 test / 4 skip / failureなし。
ログ: `.research/ui-scale/host-icons.log`、`python-icons.log`、`build-icons.log`。
実描画preview: `.research/ui-scale/status-icons-140.png`。
PaperMonoのdebug時だけTerminalのraster_usとpresent_usを分けて記録する。
raster_usは設定解決/hash/文字・アイコン描画の経過時間、present_usはdisplay呼出し。
SPI/BUSYは既存SSD1677 timingログで別に比較する。未測定値を結果扱いしない。
