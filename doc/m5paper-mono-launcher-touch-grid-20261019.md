# PaperMono Launcher 3列×2行・アプリTouch・Sleep/Wake統合

追記：承認後のApplication-only書込み結果は[Flash報告](m5paper-mono-launcher-touch-grid-flash-20261019.md)を参照。以下はFlash前に作成した実装・Buildの記録。

2026-10-19。Host／Build／Integration反映までを対象とする。**実機Flashは未実施、実機Acceptanceは未確認**。依頼書§9に従い、完成したApplication-only成果物への承認後に実施する。

## Gitと保護

| 項目 | 結果 |
|---|---|
| 作業Branch | `feature/papermono-launcher-touch-grid` |
| Worktree | `E:\ESP32\M5PapserMono-SolarOS\.research\launcher-touch-grid\worktree` |
| 基点Integration | `8cf0bed5c198a94b010a3605edbf41c21223ace5` |
| 一括実装 | `e9e6e723de76067525ae15fe9fb57bd51b85697d` |
| Reader起動オプション修正 | `2cfb6f8679587608636cdbc79c5af8f894fa9e99` |
| 最終コード／Build | `16b635c7b26a5e62fb2377cf4b285ef5ac1b84ab` |
| Integration | `integration/papermono-dirty-sketch`へfast-forward反映。コードは上記と同一。報告書CommitはGit履歴を参照。 |
| 既存Worktree | 既存31か所のBranch・HEAD・status・変更ファイルSHAを記録。Integrationの承認範囲内の更新を除き不変を照合。 |
| 実機の現在状態 | 今回はアクセスせず、従来のIntegration（共通D／Sketch A）のまま。新UIはまだ本体へ書き込んでいない。 |

基点にSketch限定Baseline例外が反映済みだったため、独自の波形選択処理は追加していない。Driver、Display／Async／GFX、Input／Session、FT6336、Sketch、TypeMouse Engine、起動設定・SDK設定を変更していない。ボードManifestにはLauncher用のUI選択defineを1つ追加しただけで、配線・電圧・SPI・キー定義は不変。

## Launcher

実際のボード構成は**横800×480・FT6336G**。依頼書の960×540・GT911へ設定を変更せず、現行Manifestを使用した。800×480と960×540のセル配置・タッチ判定をHostで検証。

| 上段左 | 上段中央 | 上段右 |
|---|---|---|
| Files | Reader | Clock |
| Keyboard | Sketch | Shell |

- 同サイズの3列×2行、計6項目。既存アイコン・UIフォント／スケールを再利用。
- HeaderはSolarOSとSleep。SleepはHeaderの操作で、7個目のアプリアイコンではない。共通Homeバーは追加していない。
- PaperMonoは固定の6項目をRAM上で構成する。既存`launcher.json`は上書き・移行しない。PaperMonoのLauncherではそのカスタム配置は使わない。他ボードのJSON Launcherは維持。
- Keyboardは既存`typemouse`。BLE名・コマンド・内部IDは変更しない。
- タッチ起動は押下と離指が同じ領域の場合に確定。押下中の選択ハイライト更新を省き、子アプリの初回描画直前に不要なLauncher更新を発生させない。CANCELで押下を破棄し、旧接触による次アプリの操作を防止。既存の矢印／Enter操作も維持。
- Shellは既存の表示Shellへ戻す。USB Shellのセッションを作り直さない。ローカルShellの空入力行でEscを押すとLauncherへ戻る。入力中のEscは従来どおり入力取消、USB Shellは切り替えない。

## 各アプリ：再利用と追加

| アプリ | 既存機能の再利用 | 今回の追加 |
|---|---|---|
| Files | TUI、2ペイン、ディレクトリ移動、opener、保存データ保護・既存確認処理 | Touch ON。行タップでカーソル選択、選択行の再タップで開く。上段Up／Prev／Next、上下スワイプでページ単位移動。MOVEごとの描画なし。 |
| Reader | 文書レイアウト、ページ／EPUB章送り、位置保存 | Touch ON。本文左／右タップで前／次、上下左右スワイプでページ送り。12pxの揺れ許容、48px以上をスワイプとし離指時1回だけ確定。HeaderのFilesから文書選択へ。 |
| Clock | RTCの日時取得、既存7セグ時計、アラーム、ストップウォッチ | Touch ON。日付表示、通常時計は分・日付が変わった時だけ描画し、秒コロン点滅を停止。既存ストップウォッチはタップで開始／停止。アラーム・ストップウォッチの既存秒表示は維持。 |
| Keyboard | 42キー、HID、Mouse、長押し固定、反転、既存Touch | Launcher表示名のみKeyboard。Engine・BLE/HID経路は変更なし。 |
| Sketch | Touch、描線・保存、非同期高速化、Baseline A由来タグ | 変更なし。既存経路を回帰検証。 |
| Shell | 物理キーボード、USB常設セッション | Launcher入口と空入力行Escでの復帰。ソフトキーボードなし。Touchを必要とするleaseを取得しない。 |

Reader入口は新しいファイル選択器を作らず、既存Filesを`--reader`モードで開く。対応するtxt／md／epubを選ぶとReaderへ渡す。ファイル選択器から開いたReaderのFilesボタンは元の選択器へ戻り、Files／Readerの循環する親子遷移を作らない。ファイル名と起動オプションを混同しないよう、既存Shellのパス引数判定も拡張した。

FilesのTouch座標はTerminalの実描画セル幅・行高・上端を問い合わせて変換する。固定の文字サイズやステータス高さを仮定しない。既存の削除・移動・リネームをTouch操作へ追加していない。

Key2/Bの**1200ms以上でEsc、短押しEnter**は変更なし。確認入力・検索中などのEscは既存の取消操作を優先するため、Launcher復帰にもう一度Escが必要な場合がある。

## Touch／Sleep／Wake

新しい入力基盤・RTOSタスクは追加せず、既存Expansionの所有者lease、SessionのCANCEL、Inputのepoch／全離指gateを使用。Files／Reader／Clockの小さな共通ヘルパーは、leaseと押下／離指の状態だけを保持する。

- 通常：Launcher／Files／Reader／Clock／Keyboard／SketchでTouchを使用。Shell自身はleaseを取得しない。LauncherからTouchアプリへ渡す間は既存leaseを保持して不要な電源再投入を避ける。
- LauncherのSleep：`solar_os_context_request_sleep`から既存`main.c::enter_sleep(false)`へ。**Light sleep**を使用し、Deep sleepへ置換しない。
- 休止前：`solar_os_sessions_quiesce_touch_consumers`で接触をCANCELし、`solar_os_expansion_set_suspended(true)`を実行。
- FT6336G：既存`stop_device`がWorker停止、入力source停止・quiesce、IRQ無効、RESET Low、**IOE0:12のTouch電源OFF**を実施。単なるイベント無視ではない。RESETはIOE0:5。現行Manifestに存在する配線を使用。
- Wake：既存KEY1/GPIO2のRTC Wakeを維持。Expansionのsuspend解除が保存された有効状態／leaseに従って電源・初期化・Workerを再開。Input／Sessionの世代更新と全離指gateで旧接触を無効化する。
- 既存KEY1のSleep/Suspend設定・RTC／Battery経路は不変。Readerなどの既存物理キー休止経路も同じExpansion停止／再開を利用する。KEY1の現在設定がSuspendなら既存のSuspend動作のまま。
- CANCEL時の互換用suspend/resumeでは、追加3アプリも再取得・再描画を行わない。通常のアプリ遷移／Wakeとは区別。

実際の消費電流・物理Wake・Wake後最初のTouch・繰り返し復帰は、今回まだ実機で確認していない。

## Host／Build

| 確認 | 結果 |
|---|---|
| 主要Host | 19実行ファイル PASS |
| 追加Host | 27実行ファイル＋復元失敗専用プロセス PASS |
| Sketch Python | 9件 PASS |
| Launcher | 6項目順序・起動先・800×480／960×540・同領域離指・CANCEL・Sleep要求 PASS |
| Files／Reader／Clock | 行・ペイン選択、親パス／既存ページ処理、タップ／スワイプ・揺れ抑制、Readerヘッダー、Clockの秒更新抑制・分更新・Esc PASS |
| Touch／Power | 取得停止／電源・IRQ・RESET・復帰、CANCEL／世代／座標変換、leaseの5回ライフサイクル、Key2長押し、RTC・電源ドライバ PASS |
| 描画回帰 | D／Sketch A例外・RAM／Shadow・Dirty・Async・Writer／Sketch保存・TypeMouse・Shell・BLE/HID PASS |
| ESP32-S3 | Compile/link PASS、ESP-IDF 5.5.5 / GCC14.2.0 |
| Board / Flavor / Layout | m5paper_mono / writerdeck / ota |
| 構成 | SPI20、fastest＋tile、Dirty rows-dual、非同期Sketch、共通D／Sketch Aを維持 |
| warning / error | 0 / 0 |
| Applicationサイズ | 3,460,928 B、OTA枠7,340,032 B、残り3,879,104 B |
| Application SHA-256 | `667a9c2461ec93f34efba07d502af7890fe24b86055897fbd10a80571e7e9d31` |
| 実機Flash | **未実施**。今回実機への読取り・書込み・設定変更なし。 |

再実行：`CJSON_DIR=<ESP-IDF components/json/cJSON> sh tests/host/run_launcher_touch_grid.sh`。Hostの仮想入力・仮想電源でのPASSを実機PASSとは扱わない。

Application-only成果物：`.research/launcher-touch-grid/artifacts/firmware-app-only.bin`。Partition画像は参照用でFlashしない。`firmware.factory.bin`は使用しない。今回の未Flash段階ではNVS／FAT／otadata／BLE bond／startupへの操作は一切ない。

前版の復元基準イメージも`artifacts/application-rollback-reference.bin`へ保管。SHA `3374c584f1424829f262aeea61245e5bb5fc24fcd2bdd941619aef00a2004845`。実際のFlash直前には、現在のポート・チップ・partition・起動slotと最新バックアップを改めて確認する。

## 承認後の実機Acceptance

1. Launcherの指定6項目・Header、各タップ起動。Key2長押しで復帰。
2. Filesで行選択／再タップ開く、親フォルダ、ページ／スワイプ。Reader入口から文書選択、前／次、Files復帰、終了。
3. ClockのRTC日付／時刻と分更新。Keyboardの固定／反転・HID、Sketch黒線／末端／終了、Shell物理入力とUSB並行応答。
4. LauncherのSleep→Touchが停止→KEY1でWake→最初のTouchから操作。数回だけ繰り返す。ReaderとShellの既存休止／復帰も確認。
5. Writer、BLE再接続、既存ファイルの再読込、Sketch Baseline Aと他アプリDを確認。異常時は操作を止め原因を切り分け、復元手順を利用する。

未解決事項は実機Acceptance。新規の電圧・波形・Dirty最適化は本タスクに含めていない。

## 変更ファイル

`boards/manifests/m5paper_mono.toml`、`src/apps/solar_os_app_touch.h`、`solar_os_launcher.c`、`solar_os_files.c`、`solar_os_reader.c`、`solar_os_clock.c`、`solar_os_shell.c`、`solar_os_type_mouse.c`、`src/services/solar_os_terminal.c/.h`、`src/shell/solar_os_shell_launch.c`。Host側は`Makefile`、新規4テスト、既存Files／Launcher／Terminal／Shell起動テスト、統合実行スクリプト。生成バイナリ除外とシェルスクリプトLF維持のため`.gitignore/.gitattributes`を追加・更新。

生ログ・Manifest・SHA・変更保護検査は`doc/evidence/launcher-touch-grid-20261019/`に保存。
