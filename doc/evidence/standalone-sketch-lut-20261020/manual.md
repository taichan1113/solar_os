# 本体だけでSketch A／D／E1を試す

対象はPaperMono／WriterDeck／SSD1677。通常起動は従来どおり通常D（SketchはA）。このUIは `SOLAR_OS_SKETCH_LUT_PANEL_TRIAL=1` の専用Buildだけに含める。USBコマンド入力なしで、本体タッチによるモード選択・同じ空Canvasへの初期化・区間開始／停止・結果確認ができる。実機でのUI・表示評価は未実施。

## 本体での操作

1. 書込み後にLauncherとTouchが正常なことを確認し、Sketchを開く。ここからUSBを外して試せる。
2. 右上の **A Trial** を押す。パネルで **A／D／E1** を選び、**New trial...** を押す。
3. 消去確認を読み、**Clear + Start** を押す。未保存Canvasは消える。保存済みPNGは変更せず、ファイル名も切り離すので試験後のSaveで元PNGを上書きしない。**Cancel** はCanvasを維持する。
4. 一度だけ白黒の初期化が完了し、右上が **A Stop／D Stop／E1 Stop** になったら、短い線を少しずつ描く。DはSketchにもDを適用する `candidate_d_sketch`。筆・黒色・太さ2・通常patternを各条件で同じに戻す。
5. 既存線の薄まり、新しい線の濃さ、白化、背景汚れ、追従性を目視する。白化や入力異常を感じたら描くのをやめ、右上の **Stop** を押す。
6. 停止パネルにLast条件、実更新数、エラー、A／D／E1／OTPの内訳が出る。目視結果と一緒に記録する。パネル表示・開始時の初期化は区間に含めない。
7. 次の条件はパネルで選び直し、**New trial... → Clear + Start**。A→D→E1の順で、各回とも短い線と同じ初期化を使う。
8. **Normal / Back** で通常D／Sketch AのCanvasへ戻る。試験停止、Sketchのsuspend／終了でも通常モードへ戻る。モードや統計をNVSへ保存しない。試験中のCanvasを自動保存しない。

## 約20更新の終了と測定上の限界

一筆のRELEASE後、最後のCanvas更新とpendingの完了を待ち、実更新数が20以上なら区間を停止して結果パネルを表示する。**一筆が長ければ20を超える**ため、厳密な20回上限ではない。短い線を使い、早めにStopを押す。描画中の各更新でstatus取得やパネル描き直しは行わない。RELEASE後に完了待ちが入る専用試験UIであり、通常Sketchと同一の連続ストローク間隔とは主張しない。

パネルへ戻ると通常Aで画面が再描画されるため、線の濃さは**Stopを押す前**に見る。区間停止後のパネル描画を波形効果へ混ぜない。停止時の全体集計は保持するが、Traceは従来どおり直近32件だけ。再起動や新しい区間開始で以前の区間情報は失われるので、本体の数字と目視結果を書き留める。USB CLIと本体パネルを同時に操作しない。

開始処理は既存Executorをdrainし、FASTEST／tile／rows-dual／20MHzとAを設定して、初期CanvasのOTP実更新2回を確認する。その後に候補を選択してtrial-startする。初期化や制御が失敗したらエラーを表示して区間を開始しない。独自LUT、電圧変更、Flash/NVS書込み、自動描画、一定間隔のpacingは追加していない。

## 次回の安全な書込み

[既存Application-only手順](../fastest-sketch-d-diagnostics/manual-ubuntu-trial.md)のGit保護、現在sessionと保存要否の確認、実機識別、Running=Boot、実表とotadataの監査、fresh全Flash backupを毎回実施する。Build済みでもこれらは省略しない。標準PlatformIO Uploadは使わない。

`firmware.bin`だけを監査で確定したApplication slotへ書き、通常起動前に全Flashを読み戻してApplication一致とApplication外の全byte不変を確認する。Bootloader／Partition Table／NVS・BLE bond／FAT・文書／otadata／PHY／非対象OTAを保護する。古い全Flash backupやpreflightを流用しない。

書込み後は通常モード `mode=D scope=all-apps-except-sketch sketch_d_trial=0`、Launcher・Touchを確認する。2026-10-20の前版では起動時Power IC読取りとI2C timeoutでLauncher切替が失敗し、USBからLauncherを一度起動するとTouchが復旧した。今回の機能はこの起動経路を修正していない。起動回帰が失敗した場合は主試験へ進まず、現在状態を保存して復旧方針を確認する。

## 再Build

専用branch `feature/papermono-standalone-lut-trial` で以下を実行する。Applicationのsize／SHAはそのPCのBuild成果物を採用する。

```bash
bash scripts/build_papermono_standalone_lut.sh
```

Host確認：`make -C tests/host sketch_lut_panel_test && tests/host/sketch_lut_panel_test`。実SketchのCanvas／pointer／PNG処理を使い、消去確認と取消、3条件、初期画像の区間除外、停止後固定、約20回のRELEASE境界停止、suspend復帰、タイムアウト／モード拒否／cleanup未実行時の開始拒否を検証する。従来のDriver／Async／USB CLIの回帰も維持する。
