# PaperMono 描画更新回数削減 Software-only MVP

実施日: 2026-10-15。判定: **CONDITIONAL**。表示内容が変わらないWriterの境界カーソル操作とSketchの同一選択で、不要な再描画・提出を削減した。既存Driverは同一Frameの物理更新を既に抑止しているため、今回のHost結果からEPD波形の実行回数削減や黒濃度改善は主張しない。実機flash・実機EPD試験は行っていない。

## 実際の描画経路

Writerは文字イベントで文書とカーソル状態を更新し、Tickで必要ならMarkdown parse/layout、全画面GFX描画、`solar_os_gfx_submit()`を実行する。SketchはタッチイベントでCanvas・UI状態を更新し、Tickで全画面GFX描画・提出する。Type/Mouseの指移動はBLE HIDへ直接送られ、EPD更新は行わない。モード切替・BLE表示状態・passkey変更時だけGFXを再描画する。これらの提出はDisplay ServiceからSSD1677用非同期Executorへ渡り、Executorが最新の不変snapshotを選び、Driverの`epd_ssd1677_present_snapshot()`へ送る。

Executorは既に2つの固定snapshot bufferで未処理要求を置き換える。`submitted`、`replaced`、`started`、`physical`は別のカウンターである。Driverは有効なShadowと現在Frameが一致すると波形を起動しない。ただし明示的な`refresh=cleanup`、初期化・復帰後など表示状態が不明な場合は同一Frameでも更新する。今回この安全条件・Driver・LUT・SPI・電源・BLE・Touchには変更していない。

## Before / After（Host）

| 同一操作シナリオ | 変更前 | 変更後 | 計測境界 |
| --- | ---: | ---: | --- |
| Writer先頭で左移動8回 | 描画要求8 | 描画要求0 | 実Writerの`render_pending`をイベントごとに測定 |
| Sketchで選択中のPenを再選択1回 | GFX提出1 | GFX提出0 | 実SketchのTickとHost提出stubで測定 |
| Sketchの同一tool・colorをタッチで再選択 | 不要な提出が起きるコード経路 | 提出0 | 変更後の実Sketch Host試験で確認。変更前は数値未測定 |
| Writerの通常編集・削除・表示 | 既存Host回帰に合格 | 同じHost回帰に合格 | 文字入力の意図的な遅延なし |
| Sketchの連続線描画・離指 | 3回の提出 | 3回の提出 | 既存Lifecycle試験。線と最終Canvasを維持 |
| Type/Mouse指移動 | EPD描画要求なし | 同じ | App経路とEngine Host試験 |
| 同一FrameのDriver Refresh | 物理更新0 | 物理更新0 | 既存SSD1677 Host試験。明示cleanupは省略しない |

Writerの新しいHostシナリオはApp内の描画要求境界を測っており、Display受付・実描画・Driver要求を直接数えた値ではない。Sketchの`GFX提出`はServiceへ入る直前のHost stub値である。物理更新0は別のDriver同一Frame試験の値で、Appシナリオの実機物理回数に外挿しない。既存Executor試験では1001件の提出を999件置換し、2件だけ開始する条件が変更前後とも維持される。入力待機時間や実機黒濃度は未測定である。

## 変更と安全条件

`src/apps/solar_os_writer.c`: カーソル位置と可視選択範囲が変わらない場合、不要なlayout/render要求を出さない。表示される選択の解除、実際の移動、非表示カーソルの再表示は従来どおり描画する。`src/apps/solar_os_sketch.c`: 既に選択中のtool・color・patternと同じ選択、および変わらない図形preview座標では再描画を要求しない。ペンの移動・線分描画・離指・Canvas変更は維持する。`tests/host`のWriter/Sketch既存fixtureに同一操作と必要表示のアサーションを追加し、図形previewは座標変更・離指による提出も確認した。

共通Display Service・Executor・SSD1677 Driverは変更していない。既存の未処理snapshot置換が有効で、Driverも同一Frameを抑止するため、より広いキュー変更やGlobal Throttleは加えなかった。明示的な更新、Suspend/Resume、App切替、失敗後の再試行は従来の経路のままである。

## 検証と判定

Writer品質Host、Sketch Lifecycle、Display Async、SSD1677、Type/Mouse Engineに加え、Writer保存/再読込・Checkpoint、Sketch Canvas/Touch/140% Lifecycleの既存Host回帰を実施。AddressSanitizer/UBSanでも変更対象のWriter、Sketch、Display Asyncを実行した。ESP32-S3のcompile/linkは`SOLAR_OS_BOARD=m5paper_mono`、`SOLAR_OS_FLAVOR=writerdeck`、`SOLAR_OS_LAYOUT=ota`、`SOLAR_OS_SKETCH_LATENCY_TRIAL=0`、`SOLAR_OS_SSD1677_WAVEFORM_TRIAL=0`で成功し、生成されたbuild選択記録のboard/flavor/layout/waveform-trialを照合した。生成されたfirmwareは**書き込んでいない**。

Hostで確認した負荷削減は有効だが、対象の無変化Frameは既存Driverが物理更新を防いでいた。従って今回の成果は「不要な描画処理と提出の削減」であり、「EPD駆動回数・パネル負担・濃度の改善」は未確認。Type/MouseのUI描画はソース追跡とビルドで確認したが、App画面のHost描画結果を直接比較していない。現在のパネルに薄まり加速の懸念があるため、実機評価への移行は別途、更新上限と停止条件を決めた場合に限る。

残課題（優先順）: (1) 実機を使わずにWriterのApp要求とExecutor/Driverカウンターを同一Host fixtureで結ぶ、(2) Type/Mouse UI状態変更時の重複描画をHostで測る、(3) SketchのCanvas clear等で完全無変化を判定できる箇所を絞る、(4) 限定的な実機評価を行うなら現在のパネル状態と停止条件を先に定義する。
