# PaperMono upstream取り込み・競合レビュー（2026-10-20）

## 結論

upstream最新版の一括マージは保留し、競合がなく独立して取り込めるSTC8Hキーボード対応・TFT電源極性対応だけを取り込む。M5Paper MONOの既存機能を改変して競合を消すことはしていない。

- 比較対象：現在の `integration/papermono-dirty-sketch`。E1／Sketch D診断などの実験Branchは対象外で、HEAD・作業内容を維持する。
- 取り込み前：`6d5c2028ae0db3c31cb8d90a7fba51f13b0b7d9a`。
- 確認したupstream/main：`2001e13f6547b03cf004edc31ac8aabce8abd330`（4.16.21系）。
- 共通祖先：`1ba98726e0c52f5147c8a565522a94d7b67b0291`。
- upstream側の未取り込み：195コミット。一括仮想マージでは24ファイルで競合。
- 取り込み先の上流履歴：`50e195dccac60a607878c9414fcce73938aa8fae`までの5コミット。マージCommit：`cb84d057`。
- この部分取り込みでSolarOS全体を4.16.21相当に更新したわけではない。残る190コミットは未取り込み。
- 実機Flash・COMポート接続・NVS/FAT/bond操作は実施しない。実機Firmwareは変わらない。

## 取り込んだ変更とPaperMonoへの影響

| Commit | 変更 | PaperMonoへの評価 |
|---|---|---|
| `5a5a96a0` | STC8H companion-MCU keypad Driver／package／manifest追加 | writerdeckの選択には追加されない。通常のFT6336・BLE入力経路は変更されない。STC8H実機動作は今回未確認 |
| `b460818d` | 上記PR #46のmerge | 履歴として取り込み |
| `c1f82976` | STC8H参考資料修正 | ドキュメントのみ |
| `b0293a00` | TFTの電源極性をboard定義で選択可能にする | SSD1677経路は変更なし。未指定時は従来と同じactive-high |
| `50e195dc` | 上記PR #42のmerge | 履歴として取り込み |

実際の差分は8ファイル、582行追加・1行削除。TFTの変更箇所は `src/services/solar_os_tft_display.c:35` と `:297`。追加packageは `packages/solar_os_packages.toml`、有効化は `flavors/full.toml` のみ。`flavors/writerdeck.toml` は変更なし。

SSD1677 Driver・LUT・SPI・Dirty Rectangle、Sketch、Writer、TypeMouse、Launcher、Input、BLE、Sleep/Wakeの既存ソースは、この部分マージによる変更なし。

## 自動マージ可能でも見送った履歴

195コミットそれぞれを取り込み前HEADに対して `git merge-tree --write-tree` で検査した。競合なしは8コミット。そのうち上記5件のみ採用した。単に変更ファイルが違うことと、安全に途中まで採用できることは区別する。

1. `bf7ecd61`／`6ac96690`（I2C controller分離）は自動マージ可能。ただし直後の `bc72478e` がcontroller所有権・default bus寿命を修正しており、この修正は既存のlight-sleep pin維持処理と競合する。最初の変更だけでは「先に開始したboard busをdefaultとして採用」「stop前にdefaultを解除」という中間実装になる。primary wiringの固定、既存controllerの再利用、解放失敗時の寿命を一緒に検討すべきであり、途中の2件だけは取り込まない。これはPaperMonoでの不具合再現結果ではなく、コードと後続修正に基づく保留判断。
2. `6740c1d2`（NMEA GNSS初版）も自動マージ可能。しかし後続にUART baud・電源状態・GGA鮮度／fix・速度範囲等の修正があり、修正版までの履歴は競合する。PaperMonoの今回の目的に不要な初版だけの導入は見送った。

根拠：[I2C初版](https://github.com/nilseuropa/solar_os/blob/6ac96690c5876ed55c5f1f834f982ab0818b39fe/src/services/solar_os_buses.c#L745)、[所有権修正](https://github.com/nilseuropa/solar_os/commit/bc72478eda4470a7835d1db3d22b08f12eb8476e)。

## 最新upstreamとの競合レビュー

以下は未取り込み最新版との比較で見つかった統合上の問題。既存実用版に現在発生している不具合という意味ではない。P1はPaperMonoの主要機能に影響し得るため、片側採用で解消しない。

| 優先度 | 競合・根拠 | PaperMonoへの影響と必要な統合方針 |
|---|---|---|
| P1 | `src/drivers/i2c_bus.c:62,84,200`。上流はcontroller管理表とdefault bus寿命を導入 | 上流だけを採ると既存のlight-sleep中のI2C pin保持を失う。こちらだけを採ると新しい所有権契約を満たさない。FT6336タッチ・PMIC等に影響するため、pin保持とcontroller登録・解放のエラー後片付けを両立させる必要あり |
| P1 | `src/main.c:513,633,934,1556`、`src/services/solar_os_sessions.c:2`、`src/services/solar_os_sessions.h:78`、`src/solar_os.c` | 上流の共通shutdown／power-state splashと、こちらの周辺機器resume・Writer checkpoint・タッチconsumer再取得・アプリ所有taskでのrebootが衝突。片側だけを採るとWake後の無反応、Launcher→TypeMouse遷移、保存保護や終了処理の回帰があり得る。両処理を残した上で実行順を検証する必要あり |
| P1 | `src/board/solar_os_board_display.h:9,25`、`src/services/solar_os_display.h:9,65`、同 `.c` | こちらのasync submissionと上流のrelax／power-state presenter追加が同じ箇所を変更。submissionを落とすとSketch高速化・snapshot所有権・BUSY中の集約を損なう。両APIの維持とsleep/shutdown時のworker排出順の設計が必要 |
| P1 | `src/services/solar_os_ble.c:412`、`solar_os_ble_backend.h:57` | こちらの末尾opcode `MOUSE_REPORT` と、上流の `HOSTS/CONNECT/FORGET/DISCONNECT` が同じ位置に追加される。上限判定を上流だけにすると、統合後のenum順によってMOUSE_REPORTが範囲外になる。全opcode・dispatcher・非同期mouse reportをセットで合わせ、ZMK CentralとPC HID Peripheralを確認する必要あり |
| P1 | `patches/nimble/overlay.cmake:13` | 既存security lifecycle overlayと、上流の同時security procedure数設定が同じ位置で競合。目的は別なので両者を維持する方針。どちらかの削除で接続・再接続の修正を失わないよう、SDKへの重複patchと実際のdefineを確認する必要あり |
| P1 | `src/services/solar_os_gfx.h:51`、同 `.c:53` | こちらの22/25/28pxフォント群・font ID保持・native/zoom対応と、上流の8px bitmap追加／フォント選択が衝突。片側採用ではWriterの拡大文字やフォントIDの意味が変わり得る。既存IDを維持して新IDを追加し、全font解決を検証する必要あり |
| P2 | `src/shell/solar_os_shell_commands.c` | 既存 `display async` と新しい `display relax` の候補・関数追加が衝突。両方を登録すべき。relaxは描画保守を伴うので、SSD1677にそのまま適用できると判断しない |
| P2 | `src/apps/solar_os_player.c`、`solar_os_webradio.c`、`src/services/solar_os_media_widgets.c` | UI scale／フォント対応と小画面向けmedia UI変更が衝突。writerdeckにもmedia機能があるため「別アプリだから影響なし」とはしない。表示サイズとbuildを確認する必要あり |
| P2 | `boards/expansion_drivers.toml` | 追加Driver定義の挿入位置が競合。PaperMono固有power／touch定義を保持し、registry重複・package整合を確認する必要あり |
| P3 | `.gitignore`、`doc/manual/boards.md`、`doc/manual/commands.md`、`doc/manual/media.input.md`、`tests/hil/README.md`、`tests/host/Makefile` | ignore／説明／テスト登録の追加衝突。双方の項目を残す必要あり。Makefileの片側採用でPaperMono回帰テストを落とさない |

上表で競合24ファイルを網羅。行番号は取り込み前Integration（`6d5c2028`）基準で、この部分マージ後も同じ。上流の対応例：[I2C](https://github.com/nilseuropa/solar_os/blob/2001e13f6547b03cf004edc31ac8aabce8abd330/src/drivers/i2c_bus.c#L68)、[Display target](https://github.com/nilseuropa/solar_os/blob/2001e13f6547b03cf004edc31ac8aabce8abd330/src/services/solar_os_display.h#L68)、[HID opcode](https://github.com/nilseuropa/solar_os/blob/2001e13f6547b03cf004edc31ac8aabce8abd330/src/services/solar_os_ble_backend.h#L57)、[shutdown](https://github.com/nilseuropa/solar_os/blob/2001e13f6547b03cf004edc31ac8aabce8abd330/src/main.c#L2095)。

上流はSSD1677の独自LUTを直接変更していないが、共通Display・GFX・SessionやFastwave／EPD保守を拡張している。したがって「SSD1677 Driverが競合しないので一括マージしてよい」とは判定できない。自動マージできたAppやbuild設定にも意味上の競合がないことを保証するものではない。

## 検証・作業保護

- 独立Branch：`integration/papermono-upstream-safe-20261020`。
- 検証Worktree：`E:\ESP32\M5PapserMono-SolarOS\.research\upstream-integration-20261020\worktree`。
- Host：`tests/host/run_launcher_touch_grid.sh` PASS。C/C++の46実行ファイル＋async restore-fail追加実行、Sketch trace Python 9件。
- 構成：`tests.test_board_manifests`＋`tests.test_flavor_packages`、WSLで75件PASS。最初のWindows実行は `_curses` 不在でboardテストのimportが失敗したため、コードを変えずWSLで再実行した。
- PaperMono ESP32-S3 build：PASS（ESP-IDF 5.5.5／GCC 14.2.0+20260121、242.92秒、Application 3,459,472 B）。結果は付属 `doc/evidence/upstream-sync-20261020/results.json` に記録する。
- `.vscode/settings.json` と、既存35 WorktreeのBranch／HEAD／未コミット内容を事前記録。最終確認では対象Integration以外を不変とし、既存の未追跡資料も維持する。
- source変更は上流の8ファイルのみ。競合解消のための独自Driver修正、LUT変更、SDK変更は行わない。
- 生ログ・仮想マージ全文・全195件の検査結果は、ローカルの `.research/upstream-integration-20261020/` に保持する。付属JSONは持ち運び用の要約。

## 続きの統合方針

次はI2Cの `bf7ecd61` と `bc72478e` を一体で扱い、light-sleep pin保持との競合を専用Branchで解消する。その後、BLE HID／NimBLE、Sleep/Wake／shutdown／Session、Display async／relax、GFXの順に分けて統合・回帰確認する。全最新版を一度に `ours`／`theirs` で解決する方法は推奨しない。

今回の結果は「安全な部分取り込みは可能、最新版全体は追加の統合作業が必要」。未取り込み変更を取り込み済みとして記録する `-s ours` mergeは行っていない。
